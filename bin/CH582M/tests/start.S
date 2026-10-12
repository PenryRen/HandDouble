.section .text.start,"ax"
.global _start
_start:
  li sp, 0x20020000
  call test_main
  ecall
1: j 1b
