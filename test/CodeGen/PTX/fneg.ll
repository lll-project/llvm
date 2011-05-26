; RUN: llc < %s -march=ptx32 | FileCheck %s

define ptx_device float @t1_f32(float %x) {
; CHECK: neg.f32 f0, f1;
; CHECK-NEXT: ret;
	%y = fsub float -0.000000e+00, %x
	ret float %y
}

define ptx_device double @t1_f64(double %x) {
; CHECK: neg.f64 fd0, fd1;
; CHECK-NEXT: ret;
	%y = fsub double -0.000000e+00, %x
	ret double %y
}
