; ModuleID = './depth3boundsSLrefsSL/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @japi1_foo_269({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** %1, i32 %2) local_unnamed_addr #0 !dbg !5 {
top:
  %3 = alloca {} addrspace(10)**, align 8
  store volatile {} addrspace(10)** %1, {} addrspace(10)*** %3, align 8
  %4 = tail call {}*** @julia.get_pgcstack()
  %5 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !7, !align !8
  %6 = bitcast {} addrspace(10)* %5 to {} addrspace(10)* addrspace(10)*, !dbg !9
  %7 = addrspacecast {} addrspace(10)* addrspace(10)* %6 to {} addrspace(10)* addrspace(11)*, !dbg !9
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 3, !dbg !9
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*, !dbg !9
  %10 = load i64, i64 addrspace(11)* %9, align 8, !dbg !9, !tbaa !13, !range !17, !invariant.load !4
  %.not.not = icmp eq i64 %10, 0, !dbg !18
  br i1 %.not.not, label %L89, label %L14.preheader, !dbg !12

L14.preheader:                                    ; preds = %top
  %11 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 4
  %12 = bitcast {} addrspace(10)* addrspace(11)* %11 to i64 addrspace(11)*
  %13 = load i64, i64 addrspace(11)* %12, align 8, !tbaa !13, !range !17, !invariant.load !4
  %.not.not6 = icmp eq i64 %13, 0
  %14 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 5
  %15 = bitcast {} addrspace(10)* addrspace(11)* %14 to i64 addrspace(11)*
  %16 = mul i64 %13, %10
  %17 = bitcast {} addrspace(10)* %5 to double addrspace(13)* addrspace(10)*
  %18 = addrspacecast double addrspace(13)* addrspace(10)* %17 to double addrspace(13)* addrspace(11)*
  br label %L14, !dbg !29

L14:                                              ; preds = %L14.preheader, %L77
  %value_phi3 = phi i64 [ %41, %L77 ], [ 1, %L14.preheader ]
  br i1 %.not.not6, label %L77, label %L29.preheader, !dbg !29

L29.preheader:                                    ; preds = %L14
  %19 = load i64, i64 addrspace(11)* %15, align 8, !tbaa !13, !range !17, !invariant.load !4
  %.not.not7 = icmp eq i64 %19, 0
  %20 = mul i64 %value_phi3, 3
  %21 = add i64 %20, 3
  %22 = mul i64 %21, %10
  %23 = add i64 %22, -1
  %24 = load double addrspace(13)*, double addrspace(13)* addrspace(11)* %18, align 8
  %25 = add nsw i64 %value_phi3, -2
  br label %L29, !dbg !30

L29:                                              ; preds = %L29.preheader, %L65
  %value_phi8 = phi i64 [ %40, %L65 ], [ 1, %L29.preheader ]
  br i1 %.not.not7, label %L65, label %L44.preheader, !dbg !30

L44.preheader:                                    ; preds = %L29
  %26 = add nsw i64 %value_phi8, -1
  %27 = mul i64 %26, %16
  %28 = add i64 %23, %27
  %29 = add nsw i64 %value_phi8, -3
  %30 = mul i64 %29, %10
  %31 = add i64 %25, %30
  br label %L44, !dbg !31

L44:                                              ; preds = %L44.preheader, %L44
  %value_phi13 = phi i64 [ %39, %L44 ], [ 1, %L44.preheader ]
  %32 = add i64 %28, %value_phi13, !dbg !32
  %33 = getelementptr inbounds double, double addrspace(13)* %24, i64 %32, !dbg !32
  %34 = load double, double addrspace(13)* %33, align 8, !dbg !32, !tbaa !34
  %35 = add nuw i64 %value_phi13, 7, !dbg !37
  %36 = mul i64 %35, %16, !dbg !37
  %37 = add i64 %31, %36, !dbg !37
  %38 = getelementptr inbounds double, double addrspace(13)* %24, i64 %37, !dbg !37
  store double %34, double addrspace(13)* %38, align 8, !dbg !37, !tbaa !34
  %.not.not8 = icmp eq i64 %value_phi13, %19, !dbg !39
  %39 = add nuw nsw i64 %value_phi13, 1, !dbg !42
  br i1 %.not.not8, label %L65.loopexit, label %L44, !dbg !31

L65.loopexit:                                     ; preds = %L44
  br label %L65, !dbg !39

L65:                                              ; preds = %L65.loopexit, %L29
  %.not = icmp eq i64 %value_phi8, %13, !dbg !39
  %40 = add nuw nsw i64 %value_phi8, 1, !dbg !42
  br i1 %.not, label %L77.loopexit, label %L29, !dbg !31

L77.loopexit:                                     ; preds = %L65
  br label %L77, !dbg !39

L77:                                              ; preds = %L77.loopexit, %L14
  %.not9 = icmp eq i64 %value_phi3, %10, !dbg !39
  %41 = add nuw nsw i64 %value_phi3, 1, !dbg !42
  br i1 %.not9, label %L89.loopexit, label %L14, !dbg !31

L89.loopexit:                                     ; preds = %L77
  br label %L89, !dbg !43

L89:                                              ; preds = %L89.loopexit, %top
  ret {} addrspace(10)* %5, !dbg !43
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth3boundsSLrefsSL/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "japi1_foo_269", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !{i64 40}
!8 = !{i64 16}
!9 = !DILocation(line: 150, scope: !10, inlinedAt: !12)
!10 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!11 = !DIFile(filename: "array.jl", directory: ".")
!12 = !DILocation(line: 4, scope: !5)
!13 = !{!14, !14, i64 0, i64 1}
!14 = !{!"jtbaa_const", !15, i64 0}
!15 = !{!"jtbaa", !16, i64 0}
!16 = !{!"jtbaa"}
!17 = !{i64 0, i64 9223372036854775807}
!18 = !DILocation(line: 83, scope: !19, inlinedAt: !21)
!19 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !20, file: !20, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!20 = !DIFile(filename: "int.jl", directory: ".")
!21 = !DILocation(line: 378, scope: !22, inlinedAt: !24)
!22 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!23 = !DIFile(filename: "operators.jl", directory: ".")
!24 = !DILocation(line: 609, scope: !25, inlinedAt: !27)
!25 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !26, file: !26, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!26 = !DIFile(filename: "range.jl", directory: ".")
!27 = !DILocation(line: 833, scope: !28, inlinedAt: !12)
!28 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !26, file: !26, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!29 = !DILocation(line: 5, scope: !5)
!30 = !DILocation(line: 6, scope: !5)
!31 = !DILocation(line: 7, scope: !5)
!32 = !DILocation(line: 862, scope: !33, inlinedAt: !31)
!33 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!34 = !{!35, !35, i64 0}
!35 = !{!"jtbaa_arraybuf", !36, i64 0}
!36 = !{!"jtbaa_data", !15, i64 0}
!37 = !DILocation(line: 905, scope: !38, inlinedAt: !31)
!38 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!39 = !DILocation(line: 468, scope: !40, inlinedAt: !42)
!40 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !41, file: !41, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!41 = !DIFile(filename: "promotion.jl", directory: ".")
!42 = !DILocation(line: 837, scope: !28, inlinedAt: !31)
!43 = !DILocation(line: 11, scope: !5)
