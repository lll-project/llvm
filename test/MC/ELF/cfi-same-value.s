// RUN: llvm-mc -filetype=obj -triple x86_64-pc-linux-gnu %s -o - | elf-dump  --dump-section-data | FileCheck %s

f:
	.cfi_startproc
        nop
        .cfi_same_value 6
        nop
	.cfi_endproc

// CHECK:       # Section 0x00000004
// CHECK-NEXT:  (('sh_name', 0x00000011) # '.eh_frame'
// CHECK-NEXT:   ('sh_type', 0x00000001)
// CHECK-NEXT:   ('sh_flags', 0x00000002)
// CHECK-NEXT:   ('sh_addr', 0x00000000)
// CHECK-NEXT:   ('sh_offset', 0x00000048)
// CHECK-NEXT:   ('sh_size', 0x00000030)
// CHECK-NEXT:   ('sh_link', 0x00000000)
// CHECK-NEXT:   ('sh_info', 0x00000000)
// CHECK-NEXT:   ('sh_addralign', 0x00000008)
// CHECK-NEXT:   ('sh_entsize', 0x00000000)
// CHECK-NEXT:   ('_section_data', '14000000 00000000 017a5200 01781001 1b0c0708 90010000 14000000 1c000000 00000000 02000000 00410806 00000000')
// CHECK-NEXT:  ),
// CHECK-NEXT:  # Section 0x00000005
// CHECK-NEXT:  (('sh_name', 0x0000000c) # '.rela.eh_frame'
// CHECK-NEXT:   ('sh_type', 0x00000004)
// CHECK-NEXT:   ('sh_flags', 0x00000000)
// CHECK-NEXT:   ('sh_addr', 0x00000000)
// CHECK-NEXT:   ('sh_offset', 0x00000390)
// CHECK-NEXT:   ('sh_size', 0x00000018)
// CHECK-NEXT:   ('sh_link', 0x00000007)
// CHECK-NEXT:   ('sh_info', 0x00000004)
// CHECK-NEXT:   ('sh_addralign', 0x00000008)
// CHECK-NEXT:   ('sh_entsize', 0x00000018)
// CHECK-NEXT:   ('_relocations', [
// CHECK-NEXT:    # Relocation 0x00000000
// CHECK-NEXT:    (('r_offset', 0x00000020)
// CHECK-NEXT:     ('r_sym', 0x00000002)
// CHECK-NEXT:     ('r_type', 0x00000002)
// CHECK-NEXT:     ('r_addend', 0x00000000)
// CHECK-NEXT:    ),
// CHECK-NEXT:   ])
// CHECK-NEXT:  ),
