/* this ALWAYS GENERATED file contains the definitions for the interfaces */


/* File created by MIDL compiler version 5.01.0164 */
/* at Wed Oct 30 18:37:40 2002
 */
/* Compiler settings for C:\Documents and Settings\mast\My Documents\VisualC\SerialEMCCD\SerialEMCCD.idl:
    Os (OptLev=s), W1, Zp8, env=Win32, ms_ext, c_ext
    error checks: allocation ref bounds_check enum stub_data 
*/
//@@MIDL_FILE_HEADING(  )


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 440
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __SerialEMCCD_h__
#define __SerialEMCCD_h__

#ifdef __cplusplus
extern "C"{
#endif 

/* Forward Declarations */ 

#ifndef __IDMCamera_FWD_DEFINED__
#define __IDMCamera_FWD_DEFINED__
typedef interface IDMCamera IDMCamera;
#endif 	/* __IDMCamera_FWD_DEFINED__ */


#ifndef __DMCamera_FWD_DEFINED__
#define __DMCamera_FWD_DEFINED__

#ifdef __cplusplus
typedef class DMCamera DMCamera;
#else
typedef struct DMCamera DMCamera;
#endif /* __cplusplus */

#endif 	/* __DMCamera_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

void __RPC_FAR * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void __RPC_FAR * ); 

#ifndef __IDMCamera_INTERFACE_DEFINED__
#define __IDMCamera_INTERFACE_DEFINED__

/* interface IDMCamera */
/* [unique][helpstring][dual][uuid][object] */ 


EXTERN_C const IID IID_IDMCamera;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E5453450-18B4-4B4C-A25E-81364F75A193")
    IDMCamera : public IDispatch
    {
    public:
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetTestArray( 
            /* [in] */ long width,
            /* [in] */ long height,
            /* [out][in] */ long __RPC_FAR *retSize,
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out] */ long __RPC_FAR *time,
            /* [out] */ unsigned long __RPC_FAR *receiptTime,
            /* [out] */ unsigned long __RPC_FAR *returnTime) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE ExecuteScript( 
            /* [in] */ long size,
            /* [size_is][in] */ long __RPC_FAR script[  ],
            /* [in] */ BOOL selectCamera,
            /* [out] */ double __RPC_FAR *retval) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE SetDebugMode( 
            /* [in] */ long debug) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE SetDMVersion( 
            /* [in] */ long version) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE SetCurrentCamera( 
            /* [in] */ long camera) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE QueueScript( 
            /* [in] */ long size,
            /* [size_is][in] */ long __RPC_FAR script[  ]) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetAcquiredImage( 
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ long processing,
            /* [in] */ double exposure,
            /* [in] */ long binning,
            /* [in] */ long top,
            /* [in] */ long left,
            /* [in] */ long bottom,
            /* [in] */ long right,
            /* [in] */ long shutter,
            /* [in] */ double settling,
            /* [in] */ long shutterDelay) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetDarkReference( 
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ double exposure,
            /* [in] */ long binning,
            /* [in] */ long top,
            /* [in] */ long left,
            /* [in] */ long bottom,
            /* [in] */ long right,
            /* [in] */ long shutter,
            /* [in] */ double settling) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetGainReference( 
            /* [size_is][out] */ float __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ long binning) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE SelectCamera( 
            /* [in] */ long camera) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetNumberOfCameras( 
            /* [out] */ long __RPC_FAR *numCameras) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IsCameraInserted( 
            /* [in] */ long camera,
            /* [out] */ BOOL __RPC_FAR *inserted) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE InsertCamera( 
            /* [in] */ long camera,
            /* [in] */ BOOL state) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetDMVersion( 
            /* [out] */ long __RPC_FAR *version) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDMCameraVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
            IDMCamera __RPC_FAR * This);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
            IDMCamera __RPC_FAR * This);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetTypeInfoCount )( 
            IDMCamera __RPC_FAR * This,
            /* [out] */ UINT __RPC_FAR *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetTypeInfo )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ ITypeInfo __RPC_FAR *__RPC_FAR *ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetIDsOfNames )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ REFIID riid,
            /* [size_is][in] */ LPOLESTR __RPC_FAR *rgszNames,
            /* [in] */ UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ DISPID __RPC_FAR *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Invoke )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS __RPC_FAR *pDispParams,
            /* [out] */ VARIANT __RPC_FAR *pVarResult,
            /* [out] */ EXCEPINFO __RPC_FAR *pExcepInfo,
            /* [out] */ UINT __RPC_FAR *puArgErr);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetTestArray )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long width,
            /* [in] */ long height,
            /* [out][in] */ long __RPC_FAR *retSize,
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out] */ long __RPC_FAR *time,
            /* [out] */ unsigned long __RPC_FAR *receiptTime,
            /* [out] */ unsigned long __RPC_FAR *returnTime);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *ExecuteScript )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long size,
            /* [size_is][in] */ long __RPC_FAR script[  ],
            /* [in] */ BOOL selectCamera,
            /* [out] */ double __RPC_FAR *retval);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SetDebugMode )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long debug);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SetDMVersion )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long version);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SetCurrentCamera )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long camera);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueueScript )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long size,
            /* [size_is][in] */ long __RPC_FAR script[  ]);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetAcquiredImage )( 
            IDMCamera __RPC_FAR * This,
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ long processing,
            /* [in] */ double exposure,
            /* [in] */ long binning,
            /* [in] */ long top,
            /* [in] */ long left,
            /* [in] */ long bottom,
            /* [in] */ long right,
            /* [in] */ long shutter,
            /* [in] */ double settling,
            /* [in] */ long shutterDelay);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetDarkReference )( 
            IDMCamera __RPC_FAR * This,
            /* [size_is][out] */ short __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ double exposure,
            /* [in] */ long binning,
            /* [in] */ long top,
            /* [in] */ long left,
            /* [in] */ long bottom,
            /* [in] */ long right,
            /* [in] */ long shutter,
            /* [in] */ double settling);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetGainReference )( 
            IDMCamera __RPC_FAR * This,
            /* [size_is][out] */ float __RPC_FAR array[  ],
            /* [out][in] */ long __RPC_FAR *arrSize,
            /* [out] */ long __RPC_FAR *width,
            /* [out] */ long __RPC_FAR *height,
            /* [in] */ long binning);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SelectCamera )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long camera);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetNumberOfCameras )( 
            IDMCamera __RPC_FAR * This,
            /* [out] */ long __RPC_FAR *numCameras);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *IsCameraInserted )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long camera,
            /* [out] */ BOOL __RPC_FAR *inserted);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *InsertCamera )( 
            IDMCamera __RPC_FAR * This,
            /* [in] */ long camera,
            /* [in] */ BOOL state);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetDMVersion )( 
            IDMCamera __RPC_FAR * This,
            /* [out] */ long __RPC_FAR *version);
        
        END_INTERFACE
    } IDMCameraVtbl;

    interface IDMCamera
    {
        CONST_VTBL struct IDMCameraVtbl __RPC_FAR *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDMCamera_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IDMCamera_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IDMCamera_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IDMCamera_GetTypeInfoCount(This,pctinfo)	\
    (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo)

#define IDMCamera_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo)

#define IDMCamera_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)

#define IDMCamera_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)


#define IDMCamera_GetTestArray(This,width,height,retSize,array,time,receiptTime,returnTime)	\
    (This)->lpVtbl -> GetTestArray(This,width,height,retSize,array,time,receiptTime,returnTime)

#define IDMCamera_ExecuteScript(This,size,script,selectCamera,retval)	\
    (This)->lpVtbl -> ExecuteScript(This,size,script,selectCamera,retval)

#define IDMCamera_SetDebugMode(This,debug)	\
    (This)->lpVtbl -> SetDebugMode(This,debug)

#define IDMCamera_SetDMVersion(This,version)	\
    (This)->lpVtbl -> SetDMVersion(This,version)

#define IDMCamera_SetCurrentCamera(This,camera)	\
    (This)->lpVtbl -> SetCurrentCamera(This,camera)

#define IDMCamera_QueueScript(This,size,script)	\
    (This)->lpVtbl -> QueueScript(This,size,script)

#define IDMCamera_GetAcquiredImage(This,array,arrSize,width,height,processing,exposure,binning,top,left,bottom,right,shutter,settling,shutterDelay)	\
    (This)->lpVtbl -> GetAcquiredImage(This,array,arrSize,width,height,processing,exposure,binning,top,left,bottom,right,shutter,settling,shutterDelay)

#define IDMCamera_GetDarkReference(This,array,arrSize,width,height,exposure,binning,top,left,bottom,right,shutter,settling)	\
    (This)->lpVtbl -> GetDarkReference(This,array,arrSize,width,height,exposure,binning,top,left,bottom,right,shutter,settling)

#define IDMCamera_GetGainReference(This,array,arrSize,width,height,binning)	\
    (This)->lpVtbl -> GetGainReference(This,array,arrSize,width,height,binning)

#define IDMCamera_SelectCamera(This,camera)	\
    (This)->lpVtbl -> SelectCamera(This,camera)

#define IDMCamera_GetNumberOfCameras(This,numCameras)	\
    (This)->lpVtbl -> GetNumberOfCameras(This,numCameras)

#define IDMCamera_IsCameraInserted(This,camera,inserted)	\
    (This)->lpVtbl -> IsCameraInserted(This,camera,inserted)

#define IDMCamera_InsertCamera(This,camera,state)	\
    (This)->lpVtbl -> InsertCamera(This,camera,state)

#define IDMCamera_GetDMVersion(This,version)	\
    (This)->lpVtbl -> GetDMVersion(This,version)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetTestArray_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long width,
    /* [in] */ long height,
    /* [out][in] */ long __RPC_FAR *retSize,
    /* [size_is][out] */ short __RPC_FAR array[  ],
    /* [out] */ long __RPC_FAR *time,
    /* [out] */ unsigned long __RPC_FAR *receiptTime,
    /* [out] */ unsigned long __RPC_FAR *returnTime);


void __RPC_STUB IDMCamera_GetTestArray_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_ExecuteScript_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long size,
    /* [size_is][in] */ long __RPC_FAR script[  ],
    /* [in] */ BOOL selectCamera,
    /* [out] */ double __RPC_FAR *retval);


void __RPC_STUB IDMCamera_ExecuteScript_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_SetDebugMode_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long debug);


void __RPC_STUB IDMCamera_SetDebugMode_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_SetDMVersion_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long version);


void __RPC_STUB IDMCamera_SetDMVersion_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_SetCurrentCamera_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long camera);


void __RPC_STUB IDMCamera_SetCurrentCamera_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_QueueScript_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long size,
    /* [size_is][in] */ long __RPC_FAR script[  ]);


void __RPC_STUB IDMCamera_QueueScript_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetAcquiredImage_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [size_is][out] */ short __RPC_FAR array[  ],
    /* [out][in] */ long __RPC_FAR *arrSize,
    /* [out] */ long __RPC_FAR *width,
    /* [out] */ long __RPC_FAR *height,
    /* [in] */ long processing,
    /* [in] */ double exposure,
    /* [in] */ long binning,
    /* [in] */ long top,
    /* [in] */ long left,
    /* [in] */ long bottom,
    /* [in] */ long right,
    /* [in] */ long shutter,
    /* [in] */ double settling,
    /* [in] */ long shutterDelay);


void __RPC_STUB IDMCamera_GetAcquiredImage_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetDarkReference_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [size_is][out] */ short __RPC_FAR array[  ],
    /* [out][in] */ long __RPC_FAR *arrSize,
    /* [out] */ long __RPC_FAR *width,
    /* [out] */ long __RPC_FAR *height,
    /* [in] */ double exposure,
    /* [in] */ long binning,
    /* [in] */ long top,
    /* [in] */ long left,
    /* [in] */ long bottom,
    /* [in] */ long right,
    /* [in] */ long shutter,
    /* [in] */ double settling);


void __RPC_STUB IDMCamera_GetDarkReference_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetGainReference_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [size_is][out] */ float __RPC_FAR array[  ],
    /* [out][in] */ long __RPC_FAR *arrSize,
    /* [out] */ long __RPC_FAR *width,
    /* [out] */ long __RPC_FAR *height,
    /* [in] */ long binning);


void __RPC_STUB IDMCamera_GetGainReference_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_SelectCamera_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long camera);


void __RPC_STUB IDMCamera_SelectCamera_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetNumberOfCameras_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [out] */ long __RPC_FAR *numCameras);


void __RPC_STUB IDMCamera_GetNumberOfCameras_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_IsCameraInserted_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long camera,
    /* [out] */ BOOL __RPC_FAR *inserted);


void __RPC_STUB IDMCamera_IsCameraInserted_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_InsertCamera_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [in] */ long camera,
    /* [in] */ BOOL state);


void __RPC_STUB IDMCamera_InsertCamera_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IDMCamera_GetDMVersion_Proxy( 
    IDMCamera __RPC_FAR * This,
    /* [out] */ long __RPC_FAR *version);


void __RPC_STUB IDMCamera_GetDMVersion_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IDMCamera_INTERFACE_DEFINED__ */



#ifndef __SERIALEMCCDLib_LIBRARY_DEFINED__
#define __SERIALEMCCDLib_LIBRARY_DEFINED__

/* library SERIALEMCCDLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_SERIALEMCCDLib;

EXTERN_C const CLSID CLSID_DMCamera;

#ifdef __cplusplus

class DECLSPEC_UUID("E3C017CA-38F5-49D2-8D1F-55024C8038C2")
DMCamera;
#endif
#endif /* __SERIALEMCCDLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif
