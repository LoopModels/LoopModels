; ModuleID = 'nestedloop_2.ll'
source_filename = "nestedloop_2!"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define void @"julia_nestedloop_2!_62"({} addrspace(10)* nonnull align 16 dereferenceable(40) %0, i64 signext %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*, !dbg !7
  %4 = addrspacecast {} addrspace(10)* addrspace(10)* %3 to {} addrspace(10)* addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %4, i64 3, !dbg !7
  %6 = bitcast {} addrspace(10)* addrspace(11)* %5 to i64 addrspace(11)*, !dbg !7
  %7 = load i64, i64 addrspace(11)* %6, align 8, !dbg !7, !tbaa !11, !range !15, !invariant.load !4
  %.not.not = icmp eq i64 %7, 0, !dbg !16
  br i1 %.not.not, label %L58, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %4, i64 4
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*
  %10 = load i64, i64 addrspace(11)* %9, align 8, !tbaa !11, !range !15, !invariant.load !4
  %.not.not8 = icmp eq i64 %10, 0
  %11 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %12 = addrspacecast i64 addrspace(13)* addrspace(10)* %11 to i64 addrspace(13)* addrspace(11)*
  br label %L14, !dbg !27

L14:                                              ; preds = %L14.preheader, %L46
  %value_phi3 = phi i64 [ %15, %L46 ], [ 1, %L14.preheader ]
  br i1 %.not.not8, label %L46, label %L29.preheader, !dbg !27

L29.preheader:                                    ; preds = %L14
  %13 = add nsw i64 %value_phi3, -1
  %14 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %12, align 8
  br label %idxend, !dbg !28

L46.loopexit:                                     ; preds = %idxend
  br label %L46, !dbg !31

L46:                                              ; preds = %L46.loopexit, %L14
  %.not9 = icmp eq i64 %value_phi3, %7, !dbg !31
  %15 = add nuw nsw i64 %value_phi3, 1, !dbg !34
  br i1 %.not9, label %L58.loopexit, label %L14, !dbg !30

L58.loopexit:                                     ; preds = %L46
  br label %L58, !dbg !30

L58:                                              ; preds = %L58.loopexit, %top
  ret void, !dbg !30

idxend:                                           ; preds = %L29.preheader, %idxend
  %value_phi8 = phi i64 [ %22, %idxend ], [ 1, %L29.preheader ]
  %16 = add nsw i64 %value_phi8, -1, !dbg !28
  %17 = mul i64 %16, %7, !dbg !28
  %18 = add i64 %17, %13, !dbg !28
  %19 = getelementptr inbounds i64, i64 addrspace(13)* %14, i64 %18, !dbg !28
  %20 = load i64, i64 addrspace(13)* %19, align 8, !dbg !28, !tbaa !35
  %21 = mul i64 %20, %1, !dbg !38
  store i64 %21, i64 addrspace(13)* %19, align 8, !dbg !40, !tbaa !35
  %.not = icmp eq i64 %value_phi8, %10, !dbg !31
  %22 = add nuw nsw i64 %value_phi8, 1, !dbg !34
  br i1 %.not, label %L46.loopexit, label %idxend, !dbg !30
}

define nonnull {} addrspace(10)* @"jfptr_nestedloop_2!_63"({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !42, !align !43
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !44, !align !44
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  tail call void @"julia_nestedloop_2!_62"({} addrspace(10)* %4, i64 signext %9) #0
  ret {} addrspace(10)* addrspacecast ({}* inttoptr (i64 140515046830088 to {}*) to {} addrspace(10)*)
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
!5 = distinct !DISubprogram(name: "nestedloop_2!", linkageName: "julia_nestedloop_2!_62", scope: null, file: !3, line: 47, type: !6, scopeLine: 47, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 150, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 48, scope: !5)
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
!27 = !DILocation(line: 49, scope: !5)
!28 = !DILocation(line: 862, scope: !29, inlinedAt: !30)
!29 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DILocation(line: 50, scope: !5)
!31 = !DILocation(line: 468, scope: !32, inlinedAt: !34)
!32 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !33, file: !33, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DIFile(filename: "promotion.jl", directory: ".")
!34 = !DILocation(line: 837, scope: !26, inlinedAt: !30)
!35 = !{!36, !36, i64 0}
!36 = !{!"jtbaa_arraybuf", !37, i64 0}
!37 = !{!"jtbaa_data", !13, i64 0}
!38 = !DILocation(line: 88, scope: !39, inlinedAt: !30)
!39 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!40 = !DILocation(line: 905, scope: !41, inlinedAt: !30)
!41 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!42 = !{i64 40}
!43 = !{i64 16}
!44 = !{i64 8}
