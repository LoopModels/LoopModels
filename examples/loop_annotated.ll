; ModuleID = 'loop.ll'
source_filename = "loop"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define i64 @julia_loop_43({} addrspace(10)* nonnull align 16 dereferenceable(40) %0) local_unnamed_addr #0 !dbg !5 {
top:
  %1 = tail call {}*** @julia.get_pgcstack()
  %2 = bitcast {} addrspace(10)* %0 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %3 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %2 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %4 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %3, i64 0, i32 1, !dbg !7
  ; %5 is trip count; 
  ; %5 - 1 is backedge count
  %5 = load i64, i64 addrspace(11)* %4, align 8, !dbg !7, !tbaa !11, !range !16
  %.not.not = icmp eq i64 %5, 0, !dbg !17
  br i1 %.not.not, label %L31, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %6 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %7 = addrspacecast i64 addrspace(13)* addrspace(10)* %6 to i64 addrspace(13)* addrspace(11)*
  %8 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %7, align 8
  br label %idxend, !dbg !28

L31.loopexit:                                     ; preds = %idxend
  %.lcssa = phi i64 [ %12, %idxend ], !dbg !31
  br label %L31, !dbg !33

L31:                                              ; preds = %L31.loopexit, %top
  %value_phi9 = phi i64 [ 0, %top ], [ %.lcssa, %L31.loopexit ]
  ret i64 %value_phi9, !dbg !33

idxend:                                           ; preds = %idxend, %L14.preheader
  ; induction variable is %value_phi3
  %value_phi3 = phi i64 [ %13, %idxend ], [ 1, %L14.preheader ] 
  %value_phi5 = phi i64 [ %12, %idxend ], [ 0, %L14.preheader ]
  %9 = add nsw i64 %value_phi3, -1, !dbg !28
  ; array %8, at index %9
  ; %value_phi3 <-- %9 <-- %10
  %10 = getelementptr inbounds i64, i64 addrspace(13)* %8, i64 %9, !dbg !28
  ; load %10
  %11 = load i64, i64 addrspace(13)* %10, align 8, !dbg !28, !tbaa !34
  %12 = add i64 %11, %value_phi5, !dbg !31
  %.not = icmp eq i64 %value_phi3, %5, !dbg !37
  %13 = add nuw nsw i64 %value_phi3, 1, !dbg !40
  br i1 %.not, label %L31.loopexit, label %idxend, !dbg !30
}

define nonnull {} addrspace(10)* @jfptr_loop_44({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !41, !align !42
  %5 = tail call i64 @julia_loop_43({} addrspace(10)* %4) #0
  %6 = tail call nonnull {} addrspace(10)* @jl_box_int64(i64 signext %5)
  ret {} addrspace(10)* %6
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

declare nonnull {} addrspace(10)* @jl_box_int64(i64 signext) local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/examples/generator.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "loop", linkageName: "julia_loop_43", scope: null, file: !3, line: 10, type: !6, scopeLine: 10, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 215, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "length;", linkageName: "length", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 12, scope: !5)
!11 = !{!12, !12, i64 0}
!12 = !{!"jtbaa_arraylen", !13, i64 0}
!13 = !{!"jtbaa_array", !14, i64 0}
!14 = !{!"jtbaa", !15, i64 0}
!15 = !{!"jtbaa"}
!16 = !{i64 0, i64 9223372036854775807}
!17 = !DILocation(line: 83, scope: !18, inlinedAt: !20)
!18 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!19 = !DIFile(filename: "int.jl", directory: ".")
!20 = !DILocation(line: 378, scope: !21, inlinedAt: !23)
!21 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !22, file: !22, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!22 = !DIFile(filename: "operators.jl", directory: ".")
!23 = !DILocation(line: 609, scope: !24, inlinedAt: !26)
!24 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !25, file: !25, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!25 = !DIFile(filename: "range.jl", directory: ".")
!26 = !DILocation(line: 833, scope: !27, inlinedAt: !10)
!27 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !25, file: !25, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!28 = !DILocation(line: 861, scope: !29, inlinedAt: !30)
!29 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DILocation(line: 13, scope: !5)
!31 = !DILocation(line: 87, scope: !32, inlinedAt: !30)
!32 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DILocation(line: 15, scope: !5)
!34 = !{!35, !35, i64 0}
!35 = !{!"jtbaa_arraybuf", !36, i64 0}
!36 = !{!"jtbaa_data", !14, i64 0}
!37 = !DILocation(line: 468, scope: !38, inlinedAt: !40)
!38 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !39, file: !39, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!39 = !DIFile(filename: "promotion.jl", directory: ".")
!40 = !DILocation(line: 837, scope: !27, inlinedAt: !30)
!41 = !{i64 40}
!42 = !{i64 16}
