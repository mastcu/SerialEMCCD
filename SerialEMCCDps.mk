
SerialEMCCDps.dll: dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj
	link /dll /out:SerialEMCCDps.dll /def:SerialEMCCDps.def \
		dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj \
		kernel32.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

SEMCCDps-GMS2-32.dll: dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj
	link /dll /out:SEMCCDps-GMS2-32.dll /def:SerialEMCCDps.def \
		dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj \
		kernel32.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

SEMCCDps-GMS2-64.dll: dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj
	link /dll /out:SEMCCDps-GMS2-64.dll /def:SerialEMCCDps.def \
		dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj \
		kernel32.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

.c.obj:
	cl /c /Ox /DWIN32 /D_WIN32_WINNT=0x0500 /DREGISTER_PROXY_DLL \
		$<

clean:
	@del SerialEMCCDps.dll
	@del SerialEMCCDps.lib
	@del SerialEMCCDps.exp
	@del SEMCCDps-GMS2-32.dll
	@del SEMCCDps-GMS2-32.lib
	@del SEMCCDps-GMS2-32.exp
	@del SEMCCDps-GMS2-64.dll
	@del SEMCCDps-GMS2-64.lib
	@del SEMCCDps-GMS2-64.exp
	@del dlldata.obj
	@del SerialEMCCD_p.obj
	@del SerialEMCCD_i.obj
