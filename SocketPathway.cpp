// SocketPathway: A module for a socket connection instead of COM connection to the
// plugin functions

#include "stdafx.h"
#include "TemplatePlugIn.h"
#include <stdio.h>
#include <winsock.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

extern PlugInWrapper gPlugInWrapper;

// Insist on at least Winsock v1.1
const unsigned int VERSION_MAJOR = 1;
const unsigned int VERSION_MINOR = 1;

#define SELECT_TIMEOUT 50

static bool sInitialized = false;
static unsigned short sPort;
static HANDLE sHSocketThread = 0;
static int sStartupError = -1;
static int sLastWSAerror = 0;
static bool sCloseForExit = false;
static char sMessageBuf[160];
static SOCKET sHListener;
static SOCKET sHClient = INVALID_SOCKET; 
static int sChunkSize = 16777216;     // Tests indicated this size was optimal
// This is exactly 4K x 2K x 2, failures occurred above twice this size
static int sSuperChunkSize = 33554432;  
static FILE *sFPdebug = NULL;

// Declarations needed on both sides
#define ARGS_BUFFER_SIZE 1024
#define MAX_LONG_ARGS 16
#define MAX_DBL_ARGS 8
#define MAX_BOOL_ARGS 8
enum {GS_ExecuteScript = 1, GS_SetDebugMode, GS_SetDMVersion, GS_SetCurrentCamera,
      GS_QueueScript, GS_GetAcquiredImage, GS_GetDarkReference, GS_GetGainReference,
      GS_SelectCamera, GS_SetReadMode, GS_GetNumberOfCameras, GS_IsCameraInserted,
      GS_InsertCamera, GS_GetDMVersion, GS_GetDMCapabilities,
      GS_SetShutterNormallyClosed, GS_SetNoDMSettling, GS_GetDSProperties,
      GS_AcquireDSImage, GS_ReturnDSChannel, GS_StopDSAcquisition, GS_CheckReferenceTime,
      GS_SetK2Parameters, GS_ChunkHandshake, GS_SetupFileSaving, GS_GetFileSaveResult,
      GS_SetupFileSaving2};

static int sNumLongSend;
static int sNumBoolSend;
static int sNumDblSend;
static int sNumLongRecv;
static int sNumBoolRecv;
static int sNumDblRecv;
static long *sLongArray;
static long sLongArgs[MAX_LONG_ARGS];   // Max is 13
static double sDoubleArgs[MAX_DBL_ARGS];  // Max is 3
static BOOL sBoolArgs[MAX_BOOL_ARGS];   // Max is 3
static char sArgsBuffer[ARGS_BUFFER_SIZE];
static int sNumBytesSend;
static BOOL sHasLongArray;

// This lists the actual arguments in and out, excluding function code in recv and
// return value on send
struct ArgDescriptor {
  int funcCode;
  int numLongRecv;
  int numBoolRecv;
  int numDblRecv;
  int numLongSend;
  int numBoolSend;
  int numDblSend;
  BOOL hasLongArray;
};

// Table of functions, with # of incoming long, bool, and double, # of outgoing
// long, bool and double, and whether there is a long array at the end, whose size is
// in the last long argument
static ArgDescriptor sFuncTable[] = {
  {GS_ExecuteScript,        1, 1, 0,   0, 0, 1,   TRUE},
  {GS_SetDebugMode,         1, 0, 0,   0, 0, 0,   FALSE},
  {GS_SetDMVersion,         1, 0, 0,   0, 0, 0,   FALSE},
  {GS_SetCurrentCamera,     1, 0, 0,   0, 0, 0,   FALSE},
  {GS_QueueScript,          1, 0, 0,   0, 0, 0,   TRUE},
  {GS_GetAcquiredImage,    13, 0, 2,   4, 0, 0,   FALSE},
  {GS_GetDarkReference,    11, 0, 2,   4, 0, 0,   FALSE},
  {GS_GetGainReference,     4, 0, 0,   4, 0, 0,   FALSE},
  {GS_SelectCamera,         1, 0, 0,   0, 0, 0,   FALSE},
  {GS_SetReadMode,          1, 0, 1,   0, 0, 0,   FALSE},
  {GS_GetNumberOfCameras,   0, 0, 0,   1, 0, 0,   FALSE},
  {GS_IsCameraInserted,     1, 0, 0,   0, 1, 0,   FALSE},
  {GS_InsertCamera,         1, 1, 0,   0, 0, 0,   FALSE},
  {GS_GetDMVersion,         0, 0, 0,   1, 0, 0,   FALSE},
  {GS_GetDMCapabilities,    0, 0, 0,   0, 3, 0,   FALSE},
  {GS_SetShutterNormallyClosed,   2, 0, 0,   0, 0, 0,   FALSE},
  {GS_SetNoDMSettling,      1, 0, 0,   0, 0, 0,   FALSE},
  {GS_GetDSProperties,      1, 0, 2,   1, 0, 3,   FALSE},
  {GS_AcquireDSImage,       7, 0, 2,   4, 0, 0,   TRUE},
  {GS_ReturnDSChannel,      5, 0, 0,   4, 0, 0,   FALSE},
  {GS_StopDSAcquisition,    0, 0, 0,   0, 0, 0,   FALSE},
  {GS_CheckReferenceTime,   1, 0, 0,   2, 0, 0,   TRUE},
  {GS_SetK2Parameters,      3, 3, 2,   0, 0, 0,   TRUE},
  {GS_SetupFileSaving,      2, 1, 1,   1, 0, 0,   TRUE},
  {GS_GetFileSaveResult,    0, 0, 0,   2, 0, 0,   FALSE},
  {GS_SetupFileSaving2,     3, 1, 5,   1, 0, 0,   TRUE},
  {-1, 0,0,0,0,0,0,FALSE}
};

// Static functions that may be called by the thread
static void Cleanup();
static DWORD WINAPI SocketProc(LPVOID pParam);
static int FinishGettingBuffer(int numReceived, int numExpected);
static int ProcessCommand(int numBytes);
static void CloseClient();
static int SendBuffer(char *buffer, int numBytes);
static void ReportErrorAndClose(int retval, const char *message);
static int SendArgsBack(int retval);
static void SendImageBack(int retval, short *imArray, int bytesPerPixel);
static int UnpackReceivedData();
static int PackDataToSend();


// Call from plugin to test for port variable and start a thread to manage sockets
int StartSocket(int &wsaError)
{
  DWORD threadID;
  WSADATA WSData;
  char *portStr = getenv("SERIALEMCCD_PORT");
  wsaError = 0;
  if (!portStr)
    return 0;
  int iPort = atoi(portStr);
  if (iPort <= 1024 || iPort > 65535)
    return 11;
  sPort = (unsigned short)iPort;

  if (WSAStartup(MAKEWORD(VERSION_MAJOR, VERSION_MINOR), &WSData)) {
    wsaError = WSAGetLastError();
    return 1;
  }
  sInitialized = true;

  // Uncomment to start a log file; use the line below to put in logged statements
  //sFPdebug = fopen("C:\\cygwin\\home\\mast\\socketDebug.txt", "w");
  //if (sFPdebug){ fprintf(sFPdebug, "Calling select\n"); fflush(sFPdebug);}
  sHSocketThread = CreateThread(NULL, 0, SocketProc, NULL, CREATE_SUSPENDED, &threadID);
  if (!sHSocketThread) {
    Cleanup();
    return 2;
  }

  // Is this an appropriate priority here?
  SetThreadPriority(sHSocketThread, THREAD_PRIORITY_HIGHEST);

  // It returns the previous suspend count; so it should be 1.
  DWORD err = ResumeThread(sHSocketThread);
  if (err < 0 || err > 1) {

    // Probably should signal the thread to shut down here as in FocusRamper
    Cleanup();
    return 3;
  }

  // Wait until the thread signals success or an error
  Sleep(2);
  while (sStartupError == -1)
    Sleep(2);

  wsaError = sLastWSAerror;
  return sStartupError;
}

// Call from plugin to shut down the socket connect and clean up everything
void ShutdownSocket(void)
{
  // This is all theoretically correct but doesn't work (not sure what that meant)
  // Currently the problem is if there is an update script running when the close
  // occurs, the wait will time out and the thread will still be active, locked in the
  // script call
  DWORD code;
  if (!sInitialized)
    return;
  sCloseForExit = true;
  WaitForSingleObject(sHSocketThread, 3 * SELECT_TIMEOUT);
  GetExitCodeThread(sHSocketThread, &code);
  if (code == STILL_ACTIVE) {
    CloseClient();
    closesocket(sHListener);

    // Suspending the thread puts it into loop at 100% CPU that eventually dies,
    // Terminating is cleaner
    TerminateThread(sHSocketThread, 1);
  }
  CloseHandle(sHSocketThread);
  Cleanup();
}


// The main socket thread routine, starts a listener, loops on getting connections and
// commands
static DWORD WINAPI SocketProc(LPVOID pParam)
{
  SOCKET hListener;
  SOCKADDR_IN sockaddr;
  struct timeval tv;
  BOOL yes = TRUE;
  int numBytes, err, numExpected;
  fd_set readFds;      // file descriptor list for select()

  // Get the listener socket
  hListener = socket(PF_INET, SOCK_STREAM, 0);
  if (hListener == INVALID_SOCKET) {
    sLastWSAerror = WSAGetLastError();
    sStartupError = 4;
    return sStartupError;
  }

  // Avoid "Address already in use" error message
  if (setsockopt(hListener, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(BOOL))) {
    sLastWSAerror = WSAGetLastError();
    sStartupError = 5;
    return sStartupError;
  }

  // Get socket address for listener on the port
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(sPort);     // short, network byte order
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  memset(sockaddr.sin_zero, '\0', sizeof(sockaddr.sin_zero));

  // Bind the listener socket to the port
  if (bind(hListener, (struct sockaddr *)(&sockaddr), sizeof(sockaddr))) {
    sLastWSAerror = WSAGetLastError();
    sStartupError = 6;
    return sStartupError;
  }
  
  tv.tv_sec = 0;
  tv.tv_usec = 1000 * SELECT_TIMEOUT;

  // Listen
  if (listen(hListener, 2)) {
    sLastWSAerror = WSAGetLastError();
    sStartupError = 7;
    return sStartupError;
  }
  sHListener = hListener;

  // Set the value to indicate we are through all the startup
  sStartupError = 0;
  
  // Loop on listening for connections and accepting them or receiving commands
  for (;;) {
    FD_ZERO(&readFds);
    FD_SET(hListener, &readFds);
    if (sHClient != INVALID_SOCKET)
      FD_SET(sHClient, &readFds);
    err = select(2, &readFds, NULL, NULL, &tv);
    if (err < 0 || sCloseForExit) {

      // Close up on error or signal from plugin
      //gPlugInWrapper.DebugToResult("Closing socket\n");
      CloseClient();
      closesocket(hListener);
      if (err < 0 && !sCloseForExit) {
        sLastWSAerror = WSAGetLastError();
        sStartupError = 7;
        sprintf(sMessageBuf, "WSA Error %d on select command\n");
        gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");
      }
        //gPlugInWrapper.DebugToResult("returning\n");
      return sStartupError;
    }

    // Just a timeout - continue the loop
    if (!err)
      continue;

    sprintf(sMessageBuf, "Select returned with Ready channel: listener %d client %d\n",
      FD_ISSET(hListener, &readFds), 
      (sHClient != INVALID_SOCKET && FD_ISSET(sHClient, &readFds)) ? 1:0);
    if (gPlugInWrapper.GetDebugVal() > 1)
      gPlugInWrapper.DebugToResult(sMessageBuf);

    // There is something to do.  Check the client first (Does ISSET Work?)
    if (sHClient != INVALID_SOCKET && FD_ISSET(sHClient, &readFds)) {
      numBytes = recv(sHClient, sArgsBuffer, ARGS_BUFFER_SIZE, 0);
 
      // Close client on error or disconnect, but allow new connect
      if (numBytes <= 0) {
        ReportErrorAndClose(numBytes, "recv from ready client");
      } else {
        memcpy(&numExpected, &sArgsBuffer[0], sizeof(int));
        if (!FinishGettingBuffer(numBytes, numExpected)) {
          sprintf(sMessageBuf, "SerialEMSocket: got %d bytes via recv on socket %d\n",
            numExpected, sHClient);
          if (gPlugInWrapper.GetDebugVal() > 1)
            gPlugInWrapper.DebugToResult(sMessageBuf);
          ProcessCommand(numExpected);
        }
      }
    }

    // Now check the listener, close an existing client if any, get new client
    if (FD_ISSET(hListener, &readFds)) {
      CloseClient();
      sHClient = accept(hListener, NULL, NULL);
      if (sHClient == INVALID_SOCKET)
        ReportErrorAndClose(SOCKET_ERROR, "accept connection from ready client\n");
      else {
        gPlugInWrapper.DebugToResult("SerialEMSocket: Accepted connection\n");
      }
    }
  }
 
  return 0;
}

// Close the socket and mark as invalid
static void CloseClient()
{
  if (sHClient == INVALID_SOCKET)
    return;
  sprintf(sMessageBuf, "SerialEMSocket: Closing socket %d\n", sHClient);
  if (!sCloseForExit)
    gPlugInWrapper.DebugToResult(sMessageBuf);
  closesocket(sHClient);
  sHClient = INVALID_SOCKET;
}

// Call the Winsock cleanup function
static void Cleanup()
{
  if (sInitialized)
    WSACleanup();
  sInitialized = false;
}

// Get the rest of the message into the buffer if it is not there yet
static int FinishGettingBuffer(int numReceived, int numExpected)
{
  int numNew, ind;
  while (numReceived < numExpected) {

    // If message is too big for buffer, just get it all and throw away the start
    ind = numReceived;
    if (numExpected > ARGS_BUFFER_SIZE)
      ind = 0;
    numNew = recv(sHClient, &sArgsBuffer[ind], ARGS_BUFFER_SIZE - ind, 0);
    if (numNew <= 0) {
      ReportErrorAndClose(numNew, "recv to get expected number of bytes\n");
      return 1;
    }
    numReceived += numNew;
  }
  return 0;
}

// Send a buffer back, in chunks if necessary
static int SendBuffer(char *buffer, int numBytes)
{
  int numTotalSent = 0;
  int numToSend, numSent;
  sprintf(sMessageBuf, "In SendBuffer, socket %d, sending %d bytes\n", sHClient,
    numBytes);
  if (gPlugInWrapper.GetDebugVal() > 1)
    gPlugInWrapper.DebugToResult(sMessageBuf);
  while (numTotalSent < numBytes) {
    numToSend = numBytes - numTotalSent;
    if (numToSend > sChunkSize)
      numToSend = sChunkSize;
    //sprintf(sMessageBuf, "Going to send %d bytes to socket %d (invalid=%d)\n",
      //numToSend, sHClient, INVALID_SOCKET);
    //gPlugInWrapper.DebugToResult(sMessageBuf);
    numSent = send(sHClient, &buffer[numTotalSent], numToSend, 0);
    if (numSent < 0) {
      ReportErrorAndClose(numSent, "send a chunk of bytes back\n");
      return 1;
    }
    numTotalSent += numSent;
  }
  return 0;
}

// Close the connection upon error; report it unless it is clearly a SerialEM disconnect
static void ReportErrorAndClose(int retval, const char *message)
{
  if (retval == SOCKET_ERROR) {
    sLastWSAerror = WSAGetLastError();
    sprintf(sMessageBuf, "WSA Error %d on call to %s\n", sLastWSAerror, message);
    if (sLastWSAerror == WSAECONNRESET)
      gPlugInWrapper.DebugToResult(sMessageBuf);
    else
      gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");
  }
  CloseClient();
}

// Close up on error or signal from plugin
static void CloseOnExitOrSelectError(int err)
{
  //gPlugInWrapper.DebugToResult("Closing socket\n");
  CloseClient();
  closesocket(sHListener);
  //if (sCloseForExit)
  //Cleanup();
  if (err < 0) {
    sLastWSAerror = WSAGetLastError();
    sStartupError = 7;
    sprintf(sMessageBuf, "WSA Error %d on select command\n");
    gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");
  }
}

// Wait for the client to acknowledge receipt of a superchunk of image
static int ListenForHandshake(int superChunk)
{
  struct timeval tv;
  int numBytes, err, numExpected, command;
  fd_set readFds;      // file descriptor list for select()
  tv.tv_sec = 0;
  tv.tv_usec = superChunk / 5;    // This is 5 MB /sec

  FD_ZERO(&readFds);
  FD_SET(sHClient, &readFds);
  err = select(1, &readFds, NULL, NULL, &tv);
  if (err < 0 || sCloseForExit) {
    CloseOnExitOrSelectError(err);
    return sStartupError;
  }

  // A timeout - close client for this so client fails
  if (!err) {
    ReportErrorAndClose(0, "timeout on handshake from client");
    return 1;
  }

  numBytes = recv(sHClient, sArgsBuffer, ARGS_BUFFER_SIZE, 0);

  // Close client on error or disconnect or too few bytes or anything wrong
  memcpy(&numExpected, &sArgsBuffer[0], sizeof(int));
  memcpy(&command, &sArgsBuffer[4], sizeof(int));
  if (command != GS_ChunkHandshake || numExpected != 8 || numBytes != 8) {
    ReportErrorAndClose(numBytes, "recv handshake from ready client");
    return 1;
  }
  return 0;
}

// Process a received message
static int ProcessCommand(int numBytes)
{
  int funcCode, ind, needed, version, rootInd, nextInd;
  short *imArray;
  char *command = NULL;
  char *refName = NULL;
  struct __stat64 statbuf;

  // Get the function code as the second element of the buffer
  if (numBytes < 8 || numBytes > ARGS_BUFFER_SIZE) {
    SendArgsBack(numBytes < 8 ? -4 : -5);  // Inadequate length or too big
    return 1;
  }
  memcpy(&funcCode, &sArgsBuffer[sizeof(int)], sizeof(long));

  // Look up the function code in the table
  ind = 0;
  while (sFuncTable[ind].funcCode >= 0 && sFuncTable[ind].funcCode != funcCode)
    ind++;
  if (sFuncTable[ind].funcCode < 0) {
    sprintf(sMessageBuf, "Function code not found: %d\n", funcCode);
    gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");
    SendArgsBack(-1);
    return 1;
  }
  
  // Set the variables for receiving and sending arguments.  Add 1 to the longs for
  // the function code coming in and the return value going out
  sNumLongRecv = sFuncTable[ind].numLongRecv + 1;
  sNumBoolRecv = sFuncTable[ind].numBoolRecv;
  sNumDblRecv = sFuncTable[ind].numDblRecv;
  sNumLongSend = sFuncTable[ind].numLongSend + 1;
  sNumBoolSend = sFuncTable[ind].numBoolSend;
  sNumDblSend = sFuncTable[ind].numDblSend;
  needed = sizeof(int) + sNumLongRecv * sizeof(long) + sNumBoolRecv * sizeof(BOOL) +
    sNumDblRecv * sizeof(double);
  if (needed > numBytes) {
    sprintf(sMessageBuf, "Command not long enough: needed = %d  numBytes = %d\n", needed, 
      numBytes);
    gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");

    SendArgsBack(-4);  // Inadequate length, don't even unpack it
    return 1;
  }
  if (UnpackReceivedData()) {
    SendArgsBack(-5);  // Message too big
    return 1;
  }
  if (sFuncTable[ind].hasLongArray)
    needed += sLongArgs[sNumLongRecv - 1] * sizeof(long);
  if (needed != numBytes) {
    sprintf(sMessageBuf, "Command wrong length: needed = %d  numBytes = %d\n", needed, 
      numBytes);
    gPlugInWrapper.ErrorToResult(sMessageBuf, "SerialEMSocket: ");
    SendArgsBack(-6);   // Wrong length
    return 1;
  }

  // ready to dispatch the command to the plugin
  // THE FUNCTION CALLS
  try {
    switch (sLongArgs[0]) {
    case GS_ExecuteScript:
      sDoubleArgs[0] = gPlugInWrapper.ExecuteScript((char *)sLongArray, sBoolArgs[0]);
      SendArgsBack(sDoubleArgs[0] == SCRIPT_ERROR_RETURN ? 1 : 0);
      break;

    case GS_SetDebugMode:
      gPlugInWrapper.SetDebugMode(sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_SetDMVersion:
      gPlugInWrapper.SetDMVersion(sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_SetCurrentCamera:
      gPlugInWrapper.SetCurrentCamera(sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_QueueScript:
      gPlugInWrapper.QueueScript((char *)sLongArray);
      SendArgsBack(0);
      break;

    case GS_GetAcquiredImage:
      imArray = new short[sLongArgs[1]];
      SendImageBack(gPlugInWrapper.GetImage(imArray, &sLongArgs[1], &sLongArgs[2], 
        &sLongArgs[3], sLongArgs[4], sDoubleArgs[0], sLongArgs[5], sLongArgs[6], 
        sLongArgs[7], sLongArgs[8], sLongArgs[9], sLongArgs[10], sDoubleArgs[1], 
        sLongArgs[11], sLongArgs[12], sLongArgs[13]), imArray, 2);
      break;

    case GS_GetDarkReference:
      imArray = new short[sLongArgs[1]];
      SendImageBack(gPlugInWrapper.GetImage(imArray, &sLongArgs[1], &sLongArgs[2], 
        &sLongArgs[3], DARK_REFERENCE, sDoubleArgs[0], sLongArgs[4], sLongArgs[5], 
        sLongArgs[6], sLongArgs[7], sLongArgs[8], sLongArgs[9], sDoubleArgs[1], 
        0, sLongArgs[10], sLongArgs[11]), imArray, 2);
      break;

    case GS_GetGainReference:
      imArray = new short[sLongArgs[1] * 2];
      SendImageBack(gPlugInWrapper.GetGainReference((float *)imArray, &sLongArgs[1], 
        &sLongArgs[2], &sLongArgs[3], sLongArgs[4]), imArray, 4);
      break;

    case GS_SelectCamera:
      SendArgsBack(gPlugInWrapper.SelectCamera(sLongArgs[1]));
      break;

    case GS_SetReadMode:
      gPlugInWrapper.SetReadMode(sLongArgs[1], sDoubleArgs[0]);
      SendArgsBack(0);
      break;

    case GS_SetK2Parameters:
      gPlugInWrapper.SetK2Parameters(sLongArgs[1], sDoubleArgs[0], sLongArgs[2], 
        sBoolArgs[0], sDoubleArgs[1], sBoolArgs[1], sBoolArgs[2], (char *)sLongArray);
      SendArgsBack(0);
      break;

    case GS_SetupFileSaving:
      ind = (int)strlen((char *)sLongArray) + 1;
      gPlugInWrapper.SetupFileSaving(sLongArgs[1], sBoolArgs[0], sDoubleArgs[0], 0,
        0., 0., 0., 0., (char *)sLongArray, (char *)sLongArray + ind, NULL, NULL, 
        &sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_SetupFileSaving2:
      rootInd = (int)strlen((char *)sLongArray) + 1;
      nextInd = rootInd;
      if (sLongArgs[2] & K2_COPY_GAIN_REF) {
        nextInd += (int)strlen((char *)sLongArray + nextInd) + 1;
        refName = (char *)sLongArray + nextInd;
      }
      if (sLongArgs[2] & K2_RUN_COMMAND) {
        nextInd += (int)strlen((char *)sLongArray + nextInd) + 1;
        command = (char *)sLongArray + nextInd;
      }
      gPlugInWrapper.SetupFileSaving(sLongArgs[1], sBoolArgs[0], sDoubleArgs[0], 
        sLongArgs[2], sDoubleArgs[1], sDoubleArgs[2], sDoubleArgs[3], sDoubleArgs[4], 
        (char *)sLongArray, (char *)sLongArray + rootInd, refName, command,
        &sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_GetFileSaveResult:
      gPlugInWrapper.GetFileSaveResult(&sLongArgs[1], &sLongArgs[2]);
      SendArgsBack(0);
      break;

    case GS_GetNumberOfCameras:
      sLongArgs[1] = gPlugInWrapper.GetNumberOfCameras();
      SendArgsBack(sLongArgs[1] < 0 ? 1: 0);
      break;

    case GS_IsCameraInserted:
      ind = gPlugInWrapper.IsCameraInserted(sLongArgs[1]);
      sBoolArgs[0] = ind > 0;
      SendArgsBack(ind < 0 ? 1: 0);
      break;

    case GS_InsertCamera:
      SendArgsBack(gPlugInWrapper.InsertCamera(sLongArgs[1], sBoolArgs[0]));
      break;

    case GS_GetDMVersion:
      sLongArgs[1] = gPlugInWrapper.GetDMVersion();
      SendArgsBack(sLongArgs[1] < 0 ? 1: 0);
      break;

    case GS_GetDMCapabilities:
      version = gPlugInWrapper.GetDMVersion();
      if (version < 0) {
        SendArgsBack(1);
        break;
      }

      // This is copied from DM camera
      sBoolArgs[0] = version < OLD_SELECT_SHUTTER_BROKEN || 
        version >= NEW_SELECT_SHUTTER_OK;
      sBoolArgs[1] = version < OLD_SETTLING_BROKEN || version >= NEW_SETTLING_OK;
      sBoolArgs[2] = version < OLD_OPEN_SHUTTER_BROKEN || 
        version >= NEW_OPEN_SHUTTER_OK;
      SendArgsBack(0);
      break;

    case GS_SetShutterNormallyClosed:
      SendArgsBack(gPlugInWrapper.SetShutterNormallyClosed(sLongArgs[1], sLongArgs[2]));
      break;

    case GS_SetNoDMSettling:
      gPlugInWrapper.SetNoDMSettling(sLongArgs[1]);
      SendArgsBack(0);
      break;

    case GS_GetDSProperties:
      SendArgsBack(gPlugInWrapper.GetDSProperties(sLongArgs[1], sDoubleArgs[0], 
        sDoubleArgs[1], &sDoubleArgs[0], &sDoubleArgs[1], &sDoubleArgs[2], 
        &sLongArgs[1]));
      break;

    case GS_AcquireDSImage:
      imArray = new short[sLongArgs[1]];
      SendImageBack(gPlugInWrapper.AcquireDSImage(imArray, &sLongArgs[1], &sLongArgs[2], 
        &sLongArgs[3], sDoubleArgs[0], sDoubleArgs[1], sLongArgs[4], sLongArgs[5], 
        sLongArgs[7], sLongArray, sLongArgs[6]), imArray, 2); 
      break;

    case GS_ReturnDSChannel:
      imArray = new short[sLongArgs[1]];
      SendImageBack(gPlugInWrapper.ReturnDSChannel(imArray, &sLongArgs[1], &sLongArgs[2], 
        &sLongArgs[3], sLongArgs[4], sLongArgs[5]), imArray, 2); 
      break;

    case GS_StopDSAcquisition:
      SendArgsBack(gPlugInWrapper.StopDSAcquisition());
      break;

    case GS_CheckReferenceTime:
      ind = _stat64((char *)sLongArray, &statbuf) ? 1 : 0;
      if (!ind) {
        memcpy(&sLongArgs[1], &statbuf.st_mtime, 2 * sizeof(long));
        sprintf(sMessageBuf, "%s has time %s\n", (char *)sLongArray, 
          _ctime64(&statbuf.st_mtime));
        gPlugInWrapper.DebugToResult(sMessageBuf);
      }
      SendArgsBack(ind);
      break;

    default:
      SendArgsBack(-1);  // Incorrect command
        break;
    }
  }
  catch (...) {
    SendArgsBack(-2);  // Memory allocation or other exception
  }
  return 0;
}

// Send the arguments back, packing the return value in the first long
static int SendArgsBack(int retval)
{
  sLongArgs[0] = retval;

  // Emergency error code is negative, just send one word back
  if (retval < 0) {
    sNumLongSend = 1;
    sNumBoolSend = 0;
    sNumDblSend = 0;
  }
  if (PackDataToSend()) {
    gPlugInWrapper.ErrorToResult("DATA BUFFER NOT BIG ENOUGH TO SEND REPLY TO SERIALEM",
      "SerialEMSocket:");
    SendArgsBack(-3);
    return 1;
  }
  return SendBuffer(sArgsBuffer, sNumBytesSend);
}

// Send the arguments from an image acquisition back then send the image if there is no
// error
static void SendImageBack(int retval, short *imArray, int bytesPerPixel)
{
  int numChunks, chunkSize, numToSend, numLeft, err, imSize, totalSent = 0;

  // determine number of superchunks and send that back as fourth long
  imSize = sLongArgs[1] * bytesPerPixel;
  numChunks = (imSize + sSuperChunkSize - 1) / sSuperChunkSize;
  sLongArgs[4] = numChunks;
  err = SendArgsBack(retval);
  sprintf(sMessageBuf, "retval = %d, err sending args %d, sending image %d in %d chunks\n"
    , retval, err, imSize, numChunks);
  gPlugInWrapper.DebugToResult(sMessageBuf);
  if (!err && !retval) {

    // Loop on the chunks until done, getting acknowledgement after each
    numLeft = imSize;
    chunkSize = (imSize + numChunks - 1) / numChunks;
    while (totalSent < imSize) {
      numToSend = chunkSize;
      if (chunkSize > imSize - totalSent)
        numToSend = imSize - totalSent;
      if (SendBuffer((char *)imArray + totalSent, numToSend))
        break;
      totalSent += numToSend;
      if (totalSent < imSize && ListenForHandshake(numToSend))
        break;
    }
  }
  delete [] imArray;
}

// Unpack the received argument buffer, skipping over the first word, the byte count
static int UnpackReceivedData()
{
  int numBytes, numUnpacked = sizeof(int);
  if (sNumLongRecv > MAX_LONG_ARGS || sNumBoolRecv > MAX_BOOL_ARGS || 
    sNumDblRecv > MAX_DBL_ARGS)
    return 1;
  numBytes = sNumLongRecv * sizeof(long);
  memcpy(sLongArgs, &sArgsBuffer[numUnpacked], numBytes);
  numUnpacked += numBytes;
  numBytes = sNumBoolRecv * sizeof(BOOL);
  if (numBytes)
    memcpy(sBoolArgs, &sArgsBuffer[numUnpacked], numBytes);
  numUnpacked += numBytes;
  numBytes = sNumDblRecv * sizeof(double);
  if (numBytes)
    memcpy(sDoubleArgs, &sArgsBuffer[numUnpacked], numBytes);
  numUnpacked += numBytes;

  // Here is the starting address of whatever comes next for the few expecting it
  sLongArray = (long *)(&sArgsBuffer[numUnpacked]);
  return 0;
}

// Pack the data into the argument buffer as longs, BOOLS, doubles
static int PackDataToSend()
{
  int numAdd;
  sNumBytesSend = sizeof(int);
  if (sNumLongSend) {
    numAdd = sNumLongSend * sizeof(long);
    if (numAdd + sNumBytesSend > ARGS_BUFFER_SIZE)
      return 1;
    memcpy(&sArgsBuffer[sNumBytesSend], sLongArgs, numAdd);
    sNumBytesSend += numAdd;
  }
  if (sNumBoolSend) {
    numAdd = sNumBoolSend * sizeof(BOOL);
    if (numAdd + sNumBytesSend > ARGS_BUFFER_SIZE)
      return 1;
    memcpy(&sArgsBuffer[sNumBytesSend], sBoolArgs, numAdd);
    sNumBytesSend += numAdd;
  }
  if (sNumDblSend) {
    numAdd = sNumDblSend * sizeof(double);
    if (numAdd + sNumBytesSend > ARGS_BUFFER_SIZE)
      return 1;
    memcpy(&sArgsBuffer[sNumBytesSend], sDoubleArgs, numAdd);
    sNumBytesSend += numAdd;
  }

  // Put the number of bytes at the beginning of the message
  memcpy(&sArgsBuffer[0], &sNumBytesSend, sizeof(int));
  return 0;
}
