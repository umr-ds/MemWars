@ECHO OFF
cl.exe /EHsc DLLInjectionProvider.cpp ../../AttackServices/DLLInjectionAttack/Injector.cpp ../../Core/MemWarsServicesCore.cpp ../../Core/MemWarsCore.c /link User32.lib Kernel32.lib Advapi32.lib Ntdll.lib Shlwapi.lib