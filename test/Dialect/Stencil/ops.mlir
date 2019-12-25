// RUN: oec-opt %s | oec-opt | FileCheck %s

func @lap(%in : !stencil.view<ijk,f64>) -> f64
  attributes { stencil.function } {
	%0 = "stencil.access"(%in) {offset = [-1, 0, 0]} : (!stencil.view<ijk,f64>) -> f64
	%1 = "stencil.access"(%in) {offset = [ 1, 0, 0]} : (!stencil.view<ijk,f64>) -> f64
	%2 = "stencil.access"(%in) {offset = [ 0, 1, 0]} : (!stencil.view<ijk,f64>) -> f64
	%3 = "stencil.access"(%in) {offset = [ 0,-1, 0]} : (!stencil.view<ijk,f64>) -> f64
	%4 = "stencil.access"(%in) {offset = [ 0, 0, 0]} : (!stencil.view<ijk,f64>) -> f64
	%5 = addf %0, %1 : f64
	%6 = addf %2, %3 : f64
	%7 = addf %5, %6 : f64
	%8 = constant -4.0 : f64
	%9 = mulf %4, %8 : f64
	%10 = addf %9, %7 : f64
	return %10 : f64
}

// CHECK-LABEL: func @lap(%{{.*}}: !stencil.view<ijk,f64>) -> f64 attributes {stencil.function} {
//  CHECK-NEXT: %{{.*}} = stencil.access %{{.*}}[-1, 0, 0] : (!stencil.view<ijk,f64>) -> f64
//  CHECK-NEXT: %{{.*}} = stencil.access %{{.*}}[1, 0, 0] : (!stencil.view<ijk,f64>) -> f64
//  CHECK-NEXT: %{{.*}} = stencil.access %{{.*}}[0, 1, 0] : (!stencil.view<ijk,f64>) -> f64
//  CHECK-NEXT: %{{.*}} = stencil.access %{{.*}}[0, -1, 0] : (!stencil.view<ijk,f64>) -> f64
//  CHECK-NEXT: %{{.*}} = stencil.access %{{.*}}[0, 0, 0] : (!stencil.view<ijk,f64>) -> f64

func @lap_stencil()
  attributes { stencil.program } {
	%in = "stencil.field"() {field="in", lb=[-3,-3,0], ub=[67,67,60]} : () -> !stencil.field<ijk,f64>
	%out = "stencil.field"() {field="out", lb=[-3,-3,0], ub=[67,67,60]} : () -> !stencil.field<ijk,f64>
	%0 = "stencil.load"(%in) : (!stencil.field<ijk,f64>) -> !stencil.view<ijk,f64>
	%1 = "stencil.apply"(%0) ({
		^bb0(%2 : !stencil.view<ijk,f64>):
		%3 = "stencil.call"(%2) { callee = @lap, offset = [0, 0, 0] } : (!stencil.view<ijk,f64>) -> f64
		"stencil.return"(%3) : (f64) -> ()
	}) : (!stencil.view<ijk,f64>) -> !stencil.view<ijk,f64>
	"stencil.store"(%1, %out) {lb=[0,0,0], ub=[64,64,60]} : (!stencil.view<ijk,f64>, !stencil.field<ijk,f64>) -> ()
	return
}

// CHECK-LABEL: func @lap_stencil() attributes {stencil.program} {
//  CHECK-NEXT: %{{.*}} = stencil.field "in" ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ijk,f64>
//  CHECK-NEXT: %{{.*}} = stencil.field "out" ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ijk,f64>
//  CHECK-NEXT: %{{.*}} = stencil.load %{{.*}} : (!stencil.field<ijk,f64>) -> !stencil.view<ijk,f64>
//  CHECK-NEXT: %{{.*}} = stencil.apply %{{.*}} = %{{.*}} : !stencil.view<ijk,f64> {
//  CHECK-NEXT: %{{.*}} = stencil.call @lap(%{{.*}})[0, 0, 0] : (!stencil.view<ijk,f64>) -> f64
//  CHECK-NEXT: stencil.return %{{.*}} : f64
//  CHECK-NEXT: } : !stencil.view<ijk,f64>
//  CHECK-NEXT: stencil.store %{{.*}} to %{{.*}} ([0, 0, 0]:[64, 64, 60]) : !stencil.view<ijk,f64> to !stencil.field<ijk,f64>
