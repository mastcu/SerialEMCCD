
SerialEMCCDps.dll: dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj
	link /dll /force:unresolved /out:SerialEMCCDps.dll /def:SerialEMCCDps.def \
		dlldata.obj SerialEMCCD_p.obj SerialEMCCD_i.obj \
		kernel32.lib rpcndr.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

.c.obj:
	cl /c /Ox /DWIN32 /D_WIN32_WINNT=0x0400 /DREGISTER_PROXY_DLL \
		$<

clean:
	@del SerialEMCCDps.dll
	@del SerialEMCCDps.lib
	@del SerialEMCCDps.exp
	@del dlldata.obj
	@del SerialEMCCD_p.obj
	@del SerialEMCCD_i.obj
