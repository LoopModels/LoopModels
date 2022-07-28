; ModuleID = 'twoloops.ll'
source_filename = "twoloops"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define i64 @julia_twoloops_51(i64 signext %0, {} addrspace(10)* nonnull align 16 dereferenceable(40) %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %1 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %4 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %3 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %4, i64 0, i32 1, !dbg !7
  %6 = load i64, i64 addrspace(11)* %5, align 8, !dbg !7, !tbaa !11, !range !16
  %7 = lshr i64 %6, 1, !dbg !17
  %8 = icmp ult i64 %6, 2, !dbg !20
  br i1 %8, label %L32, label %L15.preheader, !dbg !30

L15.preheader:                                    ; preds = %top
  %9 = bitcast {} addrspace(10)* %1 to i64 addrspace(13)* addrspace(10)*
  %10 = addrspacecast i64 addrspace(13)* addrspace(10)* %9 to i64 addrspace(13)* addrspace(11)*
  %11 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %10, align 8
  %12 = add nsw i64 %7, -1, !dbg !31
  %.not15.not = icmp ugt i64 %6, %12, !dbg !31
  br label %L15, !dbg !31

L15:                                              ; preds = %L15.preheader, %idxend
  %value_phi3 = phi i64 [ %23, %idxend ], [ 1, %L15.preheader ]
  %value_phi5 = phi i64 [ %22, %idxend ], [ %0, %L15.preheader ]
  br i1 %.not15.not, label %idxend, label %oob, !dbg !31

L32.loopexit:                                     ; preds = %idxend
  %.lcssa25 = phi i64 [ %22, %idxend ], !dbg !34
  br label %L32, !dbg !36

L32:                                              ; preds = %L32.loopexit, %top
  %value_phi9 = phi i64 [ %0, %top ], [ %.lcssa25, %L32.loopexit ]
  %.not7 = icmp ult i64 %7, %6, !dbg !36
  br i1 %.not7, label %L48.preheader, label %L65, !dbg !40

L48.preheader:                                    ; preds = %L32
  %13 = bitcast {} addrspace(10)* %1 to i64 addrspace(13)* addrspace(10)*
  %14 = addrspacecast i64 addrspace(13)* addrspace(10)* %13 to i64 addrspace(13)* addrspace(11)*
  %15 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %14, align 8, !tbaa !41, !nonnull !4
  br label %L48, !dbg !43

L48:                                              ; preds = %L48.preheader, %idxend17
  %value_phi13.in = phi i64 [ %value_phi13, %idxend17 ], [ %7, %L48.preheader ]
  %value_phi15 = phi i64 [ %28, %idxend17 ], [ %value_phi9, %L48.preheader ]
  %value_phi13 = add nuw i64 %value_phi13.in, 1, !dbg !45
  br i1 false, label %oob16, label %idxend17, !dbg !43

L65.loopexit:                                     ; preds = %idxend17
  %.lcssa = phi i64 [ %28, %idxend17 ], !dbg !46
  br label %L65, !dbg !47

L65:                                              ; preds = %L65.loopexit, %L32
  %value_phi21 = phi i64 [ %value_phi9, %L32 ], [ %.lcssa, %L65.loopexit ]
  ret i64 %value_phi21, !dbg !47

oob:                                              ; preds = %L15
  %16 = add nuw nsw i64 %6, 1, !dbg !31
  %17 = alloca i64, align 8, !dbg !31
  store i64 %16, i64* %17, align 8, !dbg !31
  %18 = addrspacecast {} addrspace(10)* %1 to {} addrspace(12)*, !dbg !31
  call void @jl_bounds_error_ints({} addrspace(12)* %18, i64* nonnull %17, i64 1), !dbg !31
  unreachable, !dbg !31

idxend:                                           ; preds = %L15
  %19 = add nsw i64 %value_phi3, -1, !dbg !31
  %20 = getelementptr inbounds i64, i64 addrspace(13)* %11, i64 %19, !dbg !31
  %21 = load i64, i64 addrspace(13)* %20, align 8, !dbg !31, !tbaa !48
  %22 = add i64 %21, %value_phi5, !dbg !34
  %.not = icmp eq i64 %value_phi3, %7, !dbg !51
  %23 = add nuw nsw i64 %value_phi3, 1, !dbg !54
  br i1 %.not, label %L32.loopexit, label %L15, !dbg !33

oob16:                                            ; preds = %L48
  %value_phi13.lcssa = phi i64 [ %value_phi13, %L48 ], !dbg !45
  %24 = alloca i64, align 8, !dbg !43
  store i64 %value_phi13.lcssa, i64* %24, align 8, !dbg !43
  %25 = addrspacecast {} addrspace(10)* %1 to {} addrspace(12)*, !dbg !43
  call void @jl_bounds_error_ints({} addrspace(12)* %25, i64* nonnull %24, i64 1), !dbg !43
  unreachable, !dbg !43

idxend17:                                         ; preds = %L48
  %26 = getelementptr inbounds i64, i64 addrspace(13)* %15, i64 %value_phi13.in, !dbg !43
  %27 = load i64, i64 addrspace(13)* %26, align 8, !dbg !43, !tbaa !48
  %28 = add i64 %27, %value_phi15, !dbg !46
  %.not8 = icmp eq i64 %value_phi13, %6, !dbg !55
  br i1 %.not8, label %L65.loopexit, label %L48, !dbg !44
}

define nonnull {} addrspace(10)* @jfptr_twoloops_52({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = bitcast {} addrspace(10)** %1 to i64 addrspace(10)**
  %5 = load i64 addrspace(10)*, i64 addrspace(10)** %4, align 8, !nonnull !4, !dereferenceable !57, !align !57
  %6 = addrspacecast i64 addrspace(10)* %5 to i64 addrspace(11)*
  %7 = load i64, i64 addrspace(11)* %6, align 8
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %9 = load {} addrspace(10)*, {} addrspace(10)** %8, align 8, !nonnull !4, !dereferenceable !58, !align !59
  %10 = tail call i64 @julia_twoloops_51(i64 signext %7, {} addrspace(10)* %9) #0
  %11 = tail call nonnull {} addrspace(10)* @jl_box_int64(i64 signext %10)
  ret {} addrspace(10)* %11
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

declare nonnull {} addrspace(10)* @jl_box_int64(i64 signext) local_unnamed_addr

; Function Attrs: noreturn
declare void @jl_bounds_error_ints({} addrspace(12)*, i64*, i64) local_unnamed_addr #2

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }
attributes #2 = { noreturn }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/examples/generator.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "twoloops", linkageName: "julia_twoloops_51", scope: null, file: !3, line: 34, type: !6, scopeLine: 34, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 215, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "length;", linkageName: "length", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 36, scope: !5)
!11 = !{!12, !12, i64 0}
!12 = !{!"jtbaa_arraylen", !13, i64 0}
!13 = !{!"jtbaa_array", !14, i64 0}
!14 = !{!"jtbaa", !15, i64 0}
!15 = !{!"jtbaa"}
!16 = !{i64 0, i64 9223372036854775807}
!17 = !DILocation(line: 284, scope: !18, inlinedAt: !10)
!18 = distinct !DISubprogram(name: "div;", linkageName: "div", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!19 = !DIFile(filename: "int.jl", directory: ".")
!20 = !DILocation(line: 83, scope: !21, inlinedAt: !22)
!21 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!22 = !DILocation(line: 378, scope: !23, inlinedAt: !25)
!23 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!24 = !DIFile(filename: "operators.jl", directory: ".")
!25 = !DILocation(line: 609, scope: !26, inlinedAt: !28)
!26 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !27, file: !27, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!27 = !DIFile(filename: "range.jl", directory: ".")
!28 = !DILocation(line: 833, scope: !29, inlinedAt: !30)
!29 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !27, file: !27, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DILocation(line: 37, scope: !5)
!31 = !DILocation(line: 861, scope: !32, inlinedAt: !33)
!32 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DILocation(line: 38, scope: !5)
!34 = !DILocation(line: 87, scope: !35, inlinedAt: !33)
!35 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!36 = !DILocation(line: 83, scope: !21, inlinedAt: !37)
!37 = !DILocation(line: 378, scope: !23, inlinedAt: !38)
!38 = !DILocation(line: 609, scope: !26, inlinedAt: !39)
!39 = !DILocation(line: 833, scope: !29, inlinedAt: !40)
!40 = !DILocation(line: 40, scope: !5)
!41 = !{!42, !42, i64 0}
!42 = !{!"jtbaa_arrayptr", !13, i64 0}
!43 = !DILocation(line: 861, scope: !32, inlinedAt: !44)
!44 = !DILocation(line: 41, scope: !5)
!45 = !DILocation(line: 0, scope: !5)
!46 = !DILocation(line: 87, scope: !35, inlinedAt: !44)
!47 = !DILocation(line: 43, scope: !5)
!48 = !{!49, !49, i64 0}
!49 = !{!"jtbaa_arraybuf", !50, i64 0}
!50 = !{!"jtbaa_data", !14, i64 0}
!51 = !DILocation(line: 468, scope: !52, inlinedAt: !54)
!52 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !53, file: !53, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!53 = !DIFile(filename: "promotion.jl", directory: ".")
!54 = !DILocation(line: 837, scope: !29, inlinedAt: !33)
!55 = !DILocation(line: 468, scope: !52, inlinedAt: !56)
!56 = !DILocation(line: 837, scope: !29, inlinedAt: !44)
!57 = !{i64 8}
!58 = !{i64 40}
!59 = !{i64 16}
