; ModuleID = 'loopstore.ll'
source_filename = "loopstore!"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define void @"julia_loopstore!_121"({} addrspace(10)* nonnull align 16 dereferenceable(40) %0, i64 signext %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %0 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %4 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %3 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %4, i64 0, i32 1, !dbg !7
  %6 = load i64, i64 addrspace(11)* %5, align 8, !dbg !7, !tbaa !11, !range !16
  %.not.not = icmp eq i64 %6, 0, !dbg !17
  br i1 %.not.not, label %L29, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %7 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %8 = addrspacecast i64 addrspace(13)* addrspace(10)* %7 to i64 addrspace(13)* addrspace(11)*
  %9 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %8, align 8
  br label %idxend, !dbg !28

L29.loopexit:                                     ; preds = %idxend
  br label %L29, !dbg !30

L29:                                              ; preds = %L29.loopexit, %top
  ret void, !dbg !30

idxend:                                           ; preds = %idxend, %L14.preheader
  %value_phi3 = phi i64 [ %12, %idxend ], [ 1, %L14.preheader ]
  %10 = add nsw i64 %value_phi3, -1, !dbg !28
  %11 = getelementptr inbounds i64, i64 addrspace(13)* %9, i64 %10, !dbg !28
  store i64 %1, i64 addrspace(13)* %11, align 8, !dbg !28, !tbaa !31
  %.not = icmp eq i64 %value_phi3, %6, !dbg !34
  %12 = add nuw nsw i64 %value_phi3, 1, !dbg !37
  br i1 %.not, label %L29.loopexit, label %idxend, !dbg !30
}

define nonnull {} addrspace(10)* @"jfptr_loopstore!_122"({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !38, !align !39
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !40, !align !40
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  tail call void @"julia_loopstore!_121"({} addrspace(10)* %4, i64 signext %9) #0
  ret {} addrspace(10)* addrspacecast ({}* inttoptr (i64 139756847849480 to {}*) to {} addrspace(10)*)
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/examples/generator.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "loopstore!", linkageName: "julia_loopstore!_121", scope: null, file: !3, line: 19, type: !6, scopeLine: 19, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 215, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "length;", linkageName: "length", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 20, scope: !5)
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
!28 = !DILocation(line: 903, scope: !29, inlinedAt: !30)
!29 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DILocation(line: 21, scope: !5)
!31 = !{!32, !32, i64 0}
!32 = !{!"jtbaa_arraybuf", !33, i64 0}
!33 = !{!"jtbaa_data", !14, i64 0}
!34 = !DILocation(line: 468, scope: !35, inlinedAt: !37)
!35 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !36, file: !36, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!36 = !DIFile(filename: "promotion.jl", directory: ".")
!37 = !DILocation(line: 837, scope: !27, inlinedAt: !30)
!38 = !{i64 40}
!39 = !{i64 16}
!40 = !{i64 8}
