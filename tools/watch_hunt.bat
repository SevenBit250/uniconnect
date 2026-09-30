@echo off
rem 观察点猎手: 盯rt_thread_defunct与看门狗槽, 命中即记录PC与现场
set OO=C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe
set GDB=C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin\riscv-none-embed-gdb.exe
cd /d C:\Users\25293\Desktop\aiworkspace\uniconnect
"%OO%" -f "%OO%\..\wch-riscv.cfg" -c "init" -c "halt" -c "reset" -c "halt" >nul 2>&1
"%GDB%" --batch -ex "set pagination off" -ex "target remote :3333" ^
  -ex "watch *(unsigned int *)0x20000518" ^
  -ex "watch *(unsigned int *)0x2000E030" ^
  -ex "continue" ^
  -ex "printf \"HIT pc=%#x mcause=%#x\\n\", $pc, $mcause" ^
  -ex "info registers pc ra sp" ^
  -ex "detach" obj/uniconnect.elf
