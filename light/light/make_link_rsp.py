import io

objs = [l.strip() for l in io.open('_link_objs.txt', encoding='ascii') if l.strip()]

parts = []
parts.append('/OUT:"D:\\YX\\LIGHT\\X64\\RELEASE\\LIGHT.EXE"')
parts.append('/NOLOGO')
parts.append('/LIBPATH:\\BUILD\\NATIVE\\X64')
parts.append('/LIBPATH:"D:\\ZLIB-1.3.2"')
parts.append('/LIBPATH:"D:\\ZLIB-1.3.2\\CONTRIB\\MINIZIP"')
parts.append('/LIBPATH:"G:\\VCPKG-2026.07.29\\INSTALLED\\X64-WINDOWS\\LIB"')
parts.append('/LIBPATH:"G:\\VCPKG-2026.07.29\\INSTALLED\\X64-WINDOWS\\LIB\\MANUAL-LINK"')

libs = ("WEBVIEW2LOADERSTATIC.LIB GDIPLUS.LIB COMCTL32.LIB OLE32.LIB OLEAUT32.LIB "
        "UUID.LIB SHELL32.LIB UXTHEME.LIB COMDLG32.LIB WINHTTP.LIB KERNEL32.LIB "
        "USER32.LIB GDI32.LIB WINSPOOL.LIB COMDLG32.LIB ADVAPI32.LIB SHELL32.LIB "
        "OLE32.LIB OLEAUT32.LIB UUID.LIB ODBC32.LIB ODBCCP32.LIB").split()
parts += libs
parts.append('"G:\\VCPKG-2026.07.29\\INSTALLED\\X64-WINDOWS\\LIB\\*.LIB"')
parts.append('/DEF:"LIGHTAPI.DEF"')
parts.append('/MANIFEST')
parts.append("/MANIFESTUAC:" + chr(34) + "level='asInvoker' uiAccess='false'" + chr(34))
parts.append('/manifest:embed')
parts.append('/manifestinput:"I:\\MICROSOFT VISUAL STUDIO\\MICROSOFT VISUAL STUDIO\\18'
             '\\COMMUNITY\\VC\\TOOLS\\MSVC\\14.51.36231\\INCLUDE\\MANIFEST\\SEGMENTHEAP.MANIFEST"')
parts.append('/DEBUG')
parts.append('/PDB:"D:\\YX\\LIGHT\\X64\\RELEASE\\LIGHT.PDB"')
parts.append('/SUBSYSTEM:CONSOLE')
parts.append('/OPT:REF')
parts.append('/OPT:ICF')
parts.append('/LTCG:incremental')
parts.append('/LTCGOUT:"X64\\RELEASE\\LIGHT.IOBJ"')
parts.append('/TLBID:1')
parts.append('/DYNAMICBASE')
parts.append('/NXCOMPAT')
parts.append('/IMPLIB:"D:\\YX\\LIGHT\\X64\\RELEASE\\LIGHT.LIB"')
parts.append('/MACHINE:X64')
parts.append('X64\\RELEASE\\LIGHT.RES')
parts += objs

io.open('link.rsp', 'w', encoding='ascii').write('\n'.join(parts) + '\n')
print('rsp written, objs:', len(objs))
