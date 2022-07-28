; ModuleID = 'dot.ll'
source_filename = "dot"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define i64 @julia_dot_47({} addrspace(10)* nonnull align 16 dereferenceable(40) %0, {} addrspace(10)* nonnull align 16 dereferenceable(40) %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %0 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %4 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %3 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %4, i64 0, i32 1, !dbg !7
  %6 = load i64, i64 addrspace(11)* %5, align 8, !dbg !7, !tbaa !11, !range !16
  %7 = bitcast {} addrspace(10)* %1 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %8 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %7 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %9 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %8, i64 0, i32 1, !dbg !7
  %10 = load i64, i64 addrspace(11)* %9, align 8, !dbg !7, !tbaa !11, !range !16
  %.not = icmp eq i64 %6, %10, !dbg !17
  br i1 %.not, label %L5, label %L40, !dbg !10

L5:                                               ; preds = %top
  %.not6.not = icmp eq i64 %6, 0, !dbg !20
  br i1 %.not6.not, label %L38, label %L19.preheader, !dbg !31

L19.preheader:                                    ; preds = %L5
  %11 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %12 = addrspacecast i64 addrspace(13)* addrspace(10)* %11 to i64 addrspace(13)* addrspace(11)*
  %13 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %12, align 8
  %14 = bitcast {} addrspace(10)* %1 to i64 addrspace(13)* addrspace(10)*
  %15 = addrspacecast i64 addrspace(13)* addrspace(10)* %14 to i64 addrspace(13)* addrspace(11)*
  %16 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %15, align 8
  %17 = add nuw nsw i64 %6, 1, !dbg !32
  br label %idxend, !dbg !32

L38.loopexit:                                     ; preds = %idxend7
  %.lcssa = phi i64 [ %28, %idxend7 ], !dbg !35
  br label %L38, !dbg !37

L38:                                              ; preds = %L38.loopexit, %L5
  %value_phi11 = phi i64 [ 0, %L5 ], [ %.lcssa, %L38.loopexit ]
  ret i64 %value_phi11, !dbg !37

L40:                                              ; preds = %top
  %18 = tail call cc37 nonnull {} addrspace(10)* bitcast ({} addrspace(10)* ({} addrspace(10)*, {} addrspace(10)**, i32)* @jl_apply_generic to {} addrspace(10)* ({} addrspace(10)*, {} addrspace(10)*)*)({} addrspace(10)* addrspacecast ({}* inttoptr (i64 139867294949392 to {}*) to {} addrspace(10)*), {} addrspace(10)* addrspacecast ({}* inttoptr (i64 139867436695376 to {}*) to {} addrspace(10)*)), !dbg !10
  %19 = addrspacecast {} addrspace(10)* %18 to {} addrspace(12)*, !dbg !10
  tail call void @jl_throw({} addrspace(12)* %19), !dbg !10
  unreachable, !dbg !10

idxend:                                           ; preds = %idxend7, %L19.preheader
  %value_phi3 = phi i64 [ %29, %idxend7 ], [ 1, %L19.preheader ]
  %value_phi5 = phi i64 [ %28, %idxend7 ], [ 0, %L19.preheader ]
  br i1 false, label %oob6, label %idxend7, !dbg !32

oob6:                                             ; preds = %idxend
  %20 = alloca i64, align 8, !dbg !32
  store i64 %17, i64* %20, align 8, !dbg !32
  %21 = addrspacecast {} addrspace(10)* %1 to {} addrspace(12)*, !dbg !32
  call void @jl_bounds_error_ints({} addrspace(12)* %21, i64* nonnull %20, i64 1), !dbg !32
  unreachable, !dbg !32

idxend7:                                          ; preds = %idxend
  %22 = add nsw i64 %value_phi3, -1, !dbg !32
  %23 = getelementptr inbounds i64, i64 addrspace(13)* %13, i64 %22, !dbg !32
  %24 = load i64, i64 addrspace(13)* %23, align 8, !dbg !32, !tbaa !38
  %25 = getelementptr inbounds i64, i64 addrspace(13)* %16, i64 %22, !dbg !32
  %26 = load i64, i64 addrspace(13)* %25, align 8, !dbg !32, !tbaa !38
  %27 = mul i64 %26, %24, !dbg !41
  %28 = add i64 %27, %value_phi5, !dbg !35
  %.not7 = icmp eq i64 %value_phi3, %6, !dbg !43
  %29 = add nuw nsw i64 %value_phi3, 1, !dbg !44
  br i1 %.not7, label %L38.loopexit, label %idxend, !dbg !34
}

define nonnull {} addrspace(10)* @jfptr_dot_48({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !45, !align !46
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = load {} addrspace(10)*, {} addrspace(10)** %5, align 8, !nonnull !4, !dereferenceable !45, !align !46
  %7 = tail call i64 @julia_dot_47({} addrspace(10)* %4, {} addrspace(10)* %6) #0
  %8 = tail call nonnull {} addrspace(10)* @jl_box_int64(i64 signext %7)
  ret {} addrspace(10)* %8
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

declare nonnull {} addrspace(10)* @jl_box_int64(i64 signext) local_unnamed_addr

; Function Attrs: noreturn
declare void @jl_bounds_error_ints({} addrspace(12)*, i64*, i64) local_unnamed_addr #2

declare nonnull {} addrspace(10)* @jl_apply_generic({} addrspace(10)*, {} addrspace(10)**, i32) local_unnamed_addr #3

; Function Attrs: noreturn
declare void @jl_throw({} addrspace(12)*) local_unnamed_addr #2

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }
attributes #2 = { noreturn }
attributes #3 = { "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/examples/generator.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "dot", linkageName: "julia_dot_47", scope: null, file: !3, line: 25, type: !6, scopeLine: 25, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 215, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "length;", linkageName: "length", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 26, scope: !5)
!11 = !{!12, !12, i64 0}
!12 = !{!"jtbaa_arraylen", !13, i64 0}
!13 = !{!"jtbaa_array", !14, i64 0}
!14 = !{!"jtbaa", !15, i64 0}
!15 = !{!"jtbaa"}
!16 = !{i64 0, i64 9223372036854775807}
!17 = !DILocation(line: 468, scope: !18, inlinedAt: !10)
!18 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!19 = !DIFile(filename: "promotion.jl", directory: ".")
!20 = !DILocation(line: 83, scope: !21, inlinedAt: !23)
!21 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !22, file: !22, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!22 = !DIFile(filename: "int.jl", directory: ".")
!23 = !DILocation(line: 378, scope: !24, inlinedAt: !26)
!24 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !25, file: !25, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!25 = !DIFile(filename: "operators.jl", directory: ".")
!26 = !DILocation(line: 609, scope: !27, inlinedAt: !29)
!27 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !28, file: !28, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!28 = !DIFile(filename: "range.jl", directory: ".")
!29 = !DILocation(line: 833, scope: !30, inlinedAt: !31)
!30 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !28, file: !28, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!31 = !DILocation(line: 28, scope: !5)
!32 = !DILocation(line: 861, scope: !33, inlinedAt: !34)
!33 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!34 = !DILocation(line: 29, scope: !5)
!35 = !DILocation(line: 87, scope: !36, inlinedAt: !34)
!36 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !22, file: !22, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!37 = !DILocation(line: 31, scope: !5)
!38 = !{!39, !39, i64 0}
!39 = !{!"jtbaa_arraybuf", !40, i64 0}
!40 = !{!"jtbaa_data", !14, i64 0}
!41 = !DILocation(line: 88, scope: !42, inlinedAt: !34)
!42 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !22, file: !22, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!43 = !DILocation(line: 468, scope: !18, inlinedAt: !44)
!44 = !DILocation(line: 837, scope: !30, inlinedAt: !34)
!45 = !{i64 40}
!46 = !{i64 16}
