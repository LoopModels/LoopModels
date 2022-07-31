; ModuleID = './depth2boundsSLrefsDN_hard/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define i64 @julia_foo_170({} addrspace(10)* nocapture nonnull readonly align 16 dereferenceable(40) %0) local_unnamed_addr #0 !dbg !5 {
top:
  %1 = tail call {}*** @julia.get_pgcstack()
  %2 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*, !dbg !7
  %3 = addrspacecast {} addrspace(10)* addrspace(10)* %2 to {} addrspace(10)* addrspace(11)*, !dbg !7
  %4 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %3, i64 3, !dbg !7
  %5 = bitcast {} addrspace(10)* addrspace(11)* %4 to i64 addrspace(11)*, !dbg !7
  %6 = load i64, i64 addrspace(11)* %5, align 8, !dbg !7, !tbaa !11, !range !15, !invariant.load !4
  %.not.not = icmp eq i64 %6, 0, !dbg !16
  br i1 %.not.not, label %L66, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %7 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %3, i64 4
  %8 = bitcast {} addrspace(10)* addrspace(11)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8, !tbaa !11, !range !15, !invariant.load !4
  %10 = tail call i64 @llvm.umax.i64(i64 %9, i64 3), !dbg !27
  %11 = icmp ult i64 %9, 4
  %12 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %13 = addrspacecast i64 addrspace(13)* addrspace(10)* %12 to i64 addrspace(13)* addrspace(11)*
  %14 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %13, align 8
  br label %L14, !dbg !28

L14:                                              ; preds = %L14.preheader, %L53
  %value_phi3 = phi i64 [ %27, %L53 ], [ 1, %L14.preheader ]
  %value_phi5 = phi i64 [ %value_phi15, %L53 ], [ 0, %L14.preheader ]
  br i1 %11, label %L53, label %L30.preheader, !dbg !28

L30.preheader:                                    ; preds = %L14
  br label %L30, !dbg !29

L30:                                              ; preds = %L30.preheader, %L30
  %value_phi9 = phi i64 [ %25, %L30 ], [ %value_phi5, %L30.preheader ]
  %value_phi10 = phi i64 [ %26, %L30 ], [ 4, %L30.preheader ]
  %15 = tail call i64 @llvm.smin.i64(i64 %value_phi10, i64 %value_phi3), !dbg !30
  %16 = add nsw i64 %value_phi10, -1, !dbg !33
  %17 = tail call i64 @llvm.smax.i64(i64 %16, i64 %value_phi3), !dbg !35
  %18 = mul i64 %17, %value_phi10, !dbg !37
  %19 = add nsw i64 %15, -1, !dbg !39
  %20 = add i64 %18, -1, !dbg !39
  %21 = mul i64 %20, %6, !dbg !39
  %22 = add i64 %19, %21, !dbg !39
  %23 = getelementptr inbounds i64, i64 addrspace(13)* %14, i64 %22, !dbg !39
  %24 = load i64, i64 addrspace(13)* %23, align 8, !dbg !39, !tbaa !41
  %25 = add i64 %24, %value_phi9, !dbg !44
  %.not.not6 = icmp eq i64 %value_phi10, %10, !dbg !46
  %26 = add nuw nsw i64 %value_phi10, 1, !dbg !48
  br i1 %.not.not6, label %L53.loopexit, label %L30, !dbg !29

L53.loopexit:                                     ; preds = %L30
  %.lcssa = phi i64 [ %25, %L30 ], !dbg !44
  br label %L53, !dbg !46

L53:                                              ; preds = %L53.loopexit, %L14
  %value_phi15 = phi i64 [ %value_phi5, %L14 ], [ %.lcssa, %L53.loopexit ]
  %.not = icmp eq i64 %value_phi3, %6, !dbg !46
  %27 = add nuw nsw i64 %value_phi3, 1, !dbg !48
  br i1 %.not, label %L66.loopexit, label %L14, !dbg !29

L66.loopexit:                                     ; preds = %L53
  %value_phi15.lcssa = phi i64 [ %value_phi15, %L53 ]
  br label %L66, !dbg !49

L66:                                              ; preds = %L66.loopexit, %top
  %value_phi19 = phi i64 [ 0, %top ], [ %value_phi15.lcssa, %L66.loopexit ]
  ret i64 %value_phi19, !dbg !49
}

define nonnull {} addrspace(10)* @jfptr_foo_171({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !50, !align !51
  %5 = tail call i64 @julia_foo_170({} addrspace(10)* %4) #0
  %6 = tail call nonnull {} addrspace(10)* @jl_box_int64(i64 signext %5)
  ret {} addrspace(10)* %6
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

declare nonnull {} addrspace(10)* @jl_box_int64(i64 signext) local_unnamed_addr

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.umax.i64(i64, i64) #2

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.smin.i64(i64, i64) #2

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.smax.i64(i64, i64) #2

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }
attributes #2 = { nocallback nofree nosync nounwind readnone speculatable willreturn }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth2boundsSLrefsDN_hard/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "julia_foo_170", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 150, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 5, scope: !5)
!11 = !{!12, !12, i64 0, i64 1}
!12 = !{!"jtbaa_const", !13, i64 0}
!13 = !{!"jtbaa", !14, i64 0}
!14 = !{!"jtbaa"}
!15 = !{i64 0, i64 9223372036854775807}
!16 = !DILocation(line: 83, scope: !17, inlinedAt: !19)
!17 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!18 = !DIFile(filename: "int.jl", directory: ".")
!19 = !DILocation(line: 378, scope: !20, inlinedAt: !22)
!20 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !21, file: !21, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!21 = !DIFile(filename: "operators.jl", directory: ".")
!22 = !DILocation(line: 609, scope: !23, inlinedAt: !25)
!23 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!24 = !DIFile(filename: "range.jl", directory: ".")
!25 = !DILocation(line: 833, scope: !26, inlinedAt: !10)
!26 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!27 = !DILocation(line: 0, scope: !5)
!28 = !DILocation(line: 6, scope: !5)
!29 = !DILocation(line: 7, scope: !5)
!30 = !DILocation(line: 480, scope: !31, inlinedAt: !29)
!31 = distinct !DISubprogram(name: "min;", linkageName: "min", scope: !32, file: !32, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!32 = !DIFile(filename: "promotion.jl", directory: ".")
!33 = !DILocation(line: 86, scope: !34, inlinedAt: !29)
!34 = distinct !DISubprogram(name: "-;", linkageName: "-", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!35 = !DILocation(line: 479, scope: !36, inlinedAt: !29)
!36 = distinct !DISubprogram(name: "max;", linkageName: "max", scope: !32, file: !32, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!37 = !DILocation(line: 88, scope: !38, inlinedAt: !29)
!38 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!39 = !DILocation(line: 862, scope: !40, inlinedAt: !29)
!40 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!41 = !{!42, !42, i64 0}
!42 = !{!"jtbaa_arraybuf", !43, i64 0}
!43 = !{!"jtbaa_data", !13, i64 0}
!44 = !DILocation(line: 87, scope: !45, inlinedAt: !29)
!45 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!46 = !DILocation(line: 468, scope: !47, inlinedAt: !48)
!47 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !32, file: !32, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!48 = !DILocation(line: 837, scope: !26, inlinedAt: !29)
!49 = !DILocation(line: 10, scope: !5)
!50 = !{i64 40}
!51 = !{i64 16}
