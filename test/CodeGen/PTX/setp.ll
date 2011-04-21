; RUN: llc < %s -march=ptx32 | FileCheck %s

define ptx_device i32 @test_setp_eq_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.eq.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp eq i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_ne_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.ne.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ne i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_lt_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.lt.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ult i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_le_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.le.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ule i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_gt_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.gt.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ugt i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_ge_u32_rr(i32 %x, i32 %y) {
; CHECK: setp.ge.u32 p0, r1, r2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp uge i32 %x, %y
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_eq_u32_ri(i32 %x) {
; CHECK: setp.eq.u32 p0, r1, 1;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp eq i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_ne_u32_ri(i32 %x) {
; CHECK: setp.ne.u32 p0, r1, 1;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ne i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_lt_u32_ri(i32 %x) {
; CHECK: setp.eq.u32 p0, r1, 0;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ult i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_le_u32_ri(i32 %x) {
; CHECK: setp.lt.u32 p0, r1, 2;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ule i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_gt_u32_ri(i32 %x) {
; CHECK: setp.gt.u32 p0, r1, 1;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp ugt i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_ge_u32_ri(i32 %x) {
; CHECK: setp.ne.u32 p0, r1, 0;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%p = icmp uge i32 %x, 1
	%z = zext i1 %p to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_4_op_format_1(i32 %x, i32 %y, i32 %u, i32 %v) {
; CHECK: setp.gt.u32 p0, r3, r4;
; CHECK-NEXT: setp.eq.and.u32 p0, r1, r2, p0;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%c = icmp eq i32 %x, %y
	%d = icmp ugt i32 %u, %v
	%e = and i1 %c, %d
	%z = zext i1 %e to i32
	ret i32 %z
}

define ptx_device i32 @test_setp_4_op_format_2(i32 %x, i32 %y, i32 %w) {
; CHECK: cvt.pred.u32 p0, r3;
; CHECK-NEXT: setp.eq.and.u32 p0, r1, r2, !p0;
; CHECK-NEXT: cvt.u32.pred r0, p0;
; CHECK-NEXT: ret;
	%c = trunc i32 %w to i1
	%d = icmp eq i32 %x, %y
	%e = xor i1 %c, 1
	%f = and i1 %d, %e
	%z = zext i1 %f to i32
	ret i32 %z
}
