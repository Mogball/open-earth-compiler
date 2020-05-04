// RUN: oec-opt %s --stencil-shape-shift | oec-opt | FileCheck %s

func @laplap(%arg0: !stencil.field<ijk,f64>, %arg1: !stencil.field<ijk,f64>) attributes {stencil.program} {
  stencil.assert %arg0 ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ijk,f64>
  stencil.assert %arg1 ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ijk,f64>
  %0 = stencil.load %arg0 ([-2, -2, 0]:[66, 66, 60]) : (!stencil.field<ijk,f64>) -> !stencil.temp<ijk,f64>
  %1 = stencil.apply %arg2 = %0 : !stencil.temp<ijk,f64> {
    %3 = stencil.access %arg2[-1, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %4 = stencil.access %arg2[1, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %5 = stencil.access %arg2[0, 1, 0] : (!stencil.temp<ijk,f64>) -> f64
    %6 = stencil.access %arg2[0, -1, 0] : (!stencil.temp<ijk,f64>) -> f64
    %7 = stencil.access %arg2[0, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %8 = addf %3, %4 : f64
    %9 = addf %5, %6 : f64
    %10 = addf %8, %9 : f64
    %cst = constant -4.000000e+00 : f64
    %11 = mulf %7, %cst : f64
    %12 = addf %11, %10 : f64
    stencil.return %12 : f64
  } to ([-1, -1, 0]:[65, 65, 60]) : !stencil.temp<ijk,f64>
  %2 = stencil.apply %arg2 = %1 : !stencil.temp<ijk,f64> {
    %3 = stencil.access %arg2[-1, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %4 = stencil.access %arg2[1, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %5 = stencil.access %arg2[0, 1, 0] : (!stencil.temp<ijk,f64>) -> f64
    %6 = stencil.access %arg2[0, -1, 0] : (!stencil.temp<ijk,f64>) -> f64
    %7 = stencil.access %arg2[0, 0, 0] : (!stencil.temp<ijk,f64>) -> f64
    %8 = addf %3, %4 : f64
    %9 = addf %5, %6 : f64
    %10 = addf %8, %9 : f64
    %cst = constant -4.000000e+00 : f64
    %11 = mulf %7, %cst : f64
    %12 = addf %11, %10 : f64
    stencil.return %12 : f64
  } to ([0, 0, 0]:[64, 64, 60]) : !stencil.temp<ijk,f64>
  stencil.store %2 to %arg1 ([0, 0, 0]:[64, 64, 60]) : !stencil.temp<ijk,f64> to !stencil.field<ijk,f64>
  return
}

// CHECK-LABEL: func @laplap(%{{.*}}: !stencil.field<ijk,f64>, %{{.*}}: !stencil.field<ijk,f64>) attributes {stencil.program} {
//  CHECK-NEXT: stencil.assert %{{.*}}([0, 0, 0] : [70, 70, 60]) : !stencil.field<ijk,f64>
//  CHECK-NEXT: stencil.assert %{{.*}}([0, 0, 0] : [70, 70, 60]) : !stencil.field<ijk,f64>
//  CHECK-NEXT: %{{.*}} = stencil.load %{{.*}}([1, 1, 0] : [69, 69, 60]) : (!stencil.field<ijk,f64>) -> !stencil.temp<ijk,f64>

//       CHECK: } to ([0, 0, 0]:[66, 66, 60]) : !stencil.temp<ijk,f64>
//       CHECK: } to ([0, 0, 0]:[64, 64, 60]) : !stencil.temp<ijk,f64>

func @lowerdim(%arg0: !stencil.field<ij,f64>, %arg1: !stencil.field<ijk,f64>) attributes {stencil.program} {
  stencil.assert %arg0 ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ij,f64>
  stencil.assert %arg1 ([-3, -3, 0]:[67, 67, 60]) : !stencil.field<ijk,f64>
  %0 = stencil.load %arg0 ([0, 0, 0]:[64, 64, 60]) : (!stencil.field<ij,f64>) -> !stencil.temp<ij,f64>
  %1 = stencil.apply %arg2 = %0 : !stencil.temp<ij,f64> {
    %2 = stencil.access %arg2[0, 0, 0] : (!stencil.temp<ij,f64>) -> f64
    stencil.return %2 : f64
  } to ([0, 0, 0]:[64, 64, 60]) : !stencil.temp<ijk,f64>
  stencil.store %1 to %arg1 ([0, 0, 0]:[64, 64, 60]) : !stencil.temp<ijk,f64> to !stencil.field<ijk,f64>
  return
}

// CHECK-LABEL: func @lowerdim(%{{.*}}: !stencil.field<ij,f64>, %{{.*}}: !stencil.field<ijk,f64>) attributes {stencil.program} {
//  CHECK-NEXT: stencil.assert %{{.*}}([0, 0, -9223372036854775808] : [70, 70, -9223372036854775808]) : !stencil.field<ij,f64>
//  CHECK-NEXT: stencil.assert %{{.*}}([0, 0, 0] : [70, 70, 60]) : !stencil.field<ijk,f64>
//  CHECK-NEXT: %{{.*}} = stencil.load %{{.*}}([3, 3, -9223372036854775808] : [67, 67, -9223372036854775808]) : (!stencil.field<ij,f64>) -> !stencil.temp<ij,f64>

//       CHECK: %{{.*}} = stencil.access %{{.*}}[0, 0, -9223372036854775808] : (!stencil.temp<ij,f64>) -> f64
