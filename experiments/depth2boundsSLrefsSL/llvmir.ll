; ModuleID = './depth2boundsSLrefsSL/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @julia_foo_257({} addrspace(10)* nonnull readonly returned align 16 dereferenceable(40) %0, i64 signext %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*, !dbg !7
  %4 = addrspacecast {} addrspace(10)* addrspace(10)* %3 to {} addrspace(10)* addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %4, i64 3, !dbg !7
  %6 = bitcast {} addrspace(10)* addrspace(11)* %5 to i64 addrspace(11)*, !dbg !7
  %7 = load i64, i64 addrspace(11)* %6, align 8, !dbg !7, !tbaa !11, !range !15, !invariant.load !4
  %.not.not = icmp eq i64 %7, 0, !dbg !16
  br i1 %.not.not, label %L61, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %4, i64 4
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*
  %10 = load i64, i64 addrspace(11)* %9, align 8, !tbaa !11, !range !15, !invariant.load !4
  %.not.not8 = icmp eq i64 %10, 0
  %11 = shl nuw i64 %7, 1
  %12 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %13 = addrspacecast i64 addrspace(13)* addrspace(10)* %12 to i64 addrspace(13)* addrspace(11)*
  %14 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %13, align 8
  br label %L14, !dbg !27

L14:                                              ; preds = %L14.preheader, %L49
  %value_phi3 = phi i64 [ %26, %L49 ], [ 1, %L14.preheader ]
  br i1 %.not.not8, label %L49, label %L29.preheader, !dbg !27

L29.preheader:                                    ; preds = %L14
  %15 = add nsw i64 %value_phi3, -1
  br label %L29, !dbg !28

L29:                                              ; preds = %L29.preheader, %L29
  %value_phi8 = phi i64 [ %25, %L29 ], [ 1, %L29.preheader ]
  %16 = mul i64 %11, %value_phi8, !dbg !29
  %17 = add i64 %15, %16, !dbg !29
  %18 = getelementptr inbounds i64, i64 addrspace(13)* %14, i64 %17, !dbg !29
  %19 = load i64, i64 addrspace(13)* %18, align 8, !dbg !29, !tbaa !31
  %20 = add i64 %19, %1, !dbg !34
  %21 = add nsw i64 %value_phi8, -1, !dbg !36
  %22 = mul i64 %21, %7, !dbg !36
  %23 = add i64 %22, %value_phi3, !dbg !36
  %24 = getelementptr inbounds i64, i64 addrspace(13)* %14, i64 %23, !dbg !36
  store i64 %20, i64 addrspace(13)* %24, align 8, !dbg !36, !tbaa !31
  %.not.not9 = icmp eq i64 %value_phi8, %10, !dbg !38
  %25 = add nuw nsw i64 %value_phi8, 1, !dbg !41
  br i1 %.not.not9, label %L49.loopexit, label %L29, !dbg !28

L49.loopexit:                                     ; preds = %L29
  br label %L49, !dbg !38

L49:                                              ; preds = %L49.loopexit, %L14
  %.not = icmp eq i64 %value_phi3, %7, !dbg !38
  %26 = add nuw nsw i64 %value_phi3, 1, !dbg !41
  br i1 %.not, label %L61.loopexit, label %L14, !dbg !28

L61.loopexit:                                     ; preds = %L49
  br label %L61, !dbg !42

L61:                                              ; preds = %L61.loopexit, %top
  ret {} addrspace(10)* %0, !dbg !42
}

define nonnull {} addrspace(10)* @jfptr_foo_258({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !43, !align !44
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !45, !align !45
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  %10 = tail call nonnull {} addrspace(10)* @julia_foo_257({} addrspace(10)* %4, i64 signext %9) #0
  %11 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8
  ret {} addrspace(10)* %11
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth2boundsSLrefsSL/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "julia_foo_257", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 150, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 4, scope: !5)
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
!27 = !DILocation(line: 5, scope: !5)
!28 = !DILocation(line: 6, scope: !5)
!29 = !DILocation(line: 862, scope: !30, inlinedAt: !28)
!30 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!31 = !{!32, !32, i64 0}
!32 = !{!"jtbaa_arraybuf", !33, i64 0}
!33 = !{!"jtbaa_data", !13, i64 0}
!34 = !DILocation(line: 87, scope: !35, inlinedAt: !28)
!35 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!36 = !DILocation(line: 905, scope: !37, inlinedAt: !28)
!37 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!38 = !DILocation(line: 468, scope: !39, inlinedAt: !41)
!39 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !40, file: !40, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!40 = !DIFile(filename: "promotion.jl", directory: ".")
!41 = !DILocation(line: 837, scope: !26, inlinedAt: !28)
!42 = !DILocation(line: 9, scope: !5)
!43 = !{i64 40}
!44 = !{i64 16}
!45 = !{i64 8}
