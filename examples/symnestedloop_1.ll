; ModuleID = 'symnestedloop_1.ll'
source_filename = "symnestedloop_1!"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @"julia_symnestedloop_1!_135"({} addrspace(10)* nonnull readonly returned align 16 dereferenceable(40) %0, i64 signext %1, i64 signext %2, i64 signext %3, i64 signext %4) local_unnamed_addr #0 !dbg !5 {
top:
  %5 = tail call {}*** @julia.get_pgcstack()
  %6 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*, !dbg !7
  %7 = addrspacecast {} addrspace(10)* addrspace(10)* %6 to {} addrspace(10)* addrspace(11)*, !dbg !7
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 3, !dbg !7
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*, !dbg !7
  %10 = load i64, i64 addrspace(11)* %9, align 8, !dbg !7, !tbaa !11, !range !15, !invariant.load !4
  %.not = icmp slt i64 %10, %1, !dbg !16
  %11 = add i64 %1, -1, !dbg !29
  %12 = select i1 %.not, i64 %11, i64 %10, !dbg !22
  %.not8 = icmp slt i64 %12, %1, !dbg !31
  br i1 %.not8, label %L61, label %L15.preheader, !dbg !10

L15.preheader:                                    ; preds = %top
  %13 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 4
  %14 = bitcast {} addrspace(10)* addrspace(11)* %13 to i64 addrspace(11)*
  %15 = load i64, i64 addrspace(11)* %14, align 8, !tbaa !11, !range !15, !invariant.load !4
  %.not9 = icmp slt i64 %15, %2
  %16 = add i64 %2, -1
  %17 = select i1 %.not9, i64 %16, i64 %15
  %.not10 = icmp slt i64 %17, %2
  %18 = xor i64 %3, -1
  %19 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %20 = addrspacecast i64 addrspace(13)* addrspace(10)* %19 to i64 addrspace(13)* addrspace(11)*
  %21 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %20, align 8
  br label %L15, !dbg !39

L15:                                              ; preds = %L15.preheader, %L49
  %value_phi3 = phi i64 [ %34, %L49 ], [ %1, %L15.preheader ]
  br i1 %.not10, label %L49, label %L31.preheader, !dbg !39

L31.preheader:                                    ; preds = %L15
  %22 = add i64 %value_phi3, %18
  %23 = add i64 %value_phi3, -1
  br label %L31, !dbg !40

L31:                                              ; preds = %L31.preheader, %L31
  %value_phi8 = phi i64 [ %33, %L31 ], [ %2, %L31.preheader ]
  %24 = add i64 %value_phi8, -1, !dbg !41
  %25 = add i64 %24, %4, !dbg !43
  %26 = mul i64 %25, %10, !dbg !43
  %27 = add i64 %22, %26, !dbg !43
  %28 = getelementptr inbounds i64, i64 addrspace(13)* %21, i64 %27, !dbg !43
  %29 = load i64, i64 addrspace(13)* %28, align 8, !dbg !43, !tbaa !45
  %30 = mul i64 %24, %10, !dbg !48
  %31 = add i64 %23, %30, !dbg !48
  %32 = getelementptr inbounds i64, i64 addrspace(13)* %21, i64 %31, !dbg !48
  store i64 %29, i64 addrspace(13)* %32, align 8, !dbg !48, !tbaa !45
  %.not11.not = icmp eq i64 %value_phi8, %17, !dbg !50
  %33 = add i64 %value_phi8, 1, !dbg !53
  br i1 %.not11.not, label %L49.loopexit, label %L31, !dbg !40

L49.loopexit:                                     ; preds = %L31
  br label %L49, !dbg !50

L49:                                              ; preds = %L49.loopexit, %L15
  %.not12 = icmp eq i64 %value_phi3, %12, !dbg !50
  %34 = add i64 %value_phi3, 1, !dbg !53
  br i1 %.not12, label %L61.loopexit, label %L15, !dbg !40

L61.loopexit:                                     ; preds = %L49
  br label %L61, !dbg !54

L61:                                              ; preds = %L61.loopexit, %top
  ret {} addrspace(10)* %0, !dbg !54
}

define nonnull {} addrspace(10)* @"jfptr_symnestedloop_1!_136"({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !55, !align !56
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !57, !align !57
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  %10 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 2
  %11 = bitcast {} addrspace(10)** %10 to i64 addrspace(10)**
  %12 = load i64 addrspace(10)*, i64 addrspace(10)** %11, align 8, !nonnull !4, !dereferenceable !57, !align !57
  %13 = addrspacecast i64 addrspace(10)* %12 to i64 addrspace(11)*
  %14 = load i64, i64 addrspace(11)* %13, align 8
  %15 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 3
  %16 = bitcast {} addrspace(10)** %15 to i64 addrspace(10)**
  %17 = load i64 addrspace(10)*, i64 addrspace(10)** %16, align 8, !nonnull !4, !dereferenceable !57, !align !57
  %18 = addrspacecast i64 addrspace(10)* %17 to i64 addrspace(11)*
  %19 = load i64, i64 addrspace(11)* %18, align 8
  %20 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 4
  %21 = bitcast {} addrspace(10)** %20 to i64 addrspace(10)**
  %22 = load i64 addrspace(10)*, i64 addrspace(10)** %21, align 8, !nonnull !4, !dereferenceable !57, !align !57
  %23 = addrspacecast i64 addrspace(10)* %22 to i64 addrspace(11)*
  %24 = load i64, i64 addrspace(11)* %23, align 8
  %25 = tail call nonnull {} addrspace(10)* @"julia_symnestedloop_1!_135"({} addrspace(10)* %4, i64 signext %9, i64 signext %14, i64 signext %19, i64 signext %24) #0
  %26 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8
  ret {} addrspace(10)* %26
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
!5 = distinct !DISubprogram(name: "symnestedloop_1!", linkageName: "julia_symnestedloop_1!_135", scope: null, file: !3, line: 80, type: !6, scopeLine: 80, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 150, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 81, scope: !5)
!11 = !{!12, !12, i64 0, i64 1}
!12 = !{!"jtbaa_const", !13, i64 0}
!13 = !{!"jtbaa", !14, i64 0}
!14 = !{!"jtbaa"}
!15 = !{i64 0, i64 9223372036854775807}
!16 = !DILocation(line: 477, scope: !17, inlinedAt: !19)
!17 = distinct !DISubprogram(name: "<=;", linkageName: "<=", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!18 = !DIFile(filename: "int.jl", directory: ".")
!19 = !DILocation(line: 425, scope: !20, inlinedAt: !22)
!20 = distinct !DISubprogram(name: ">=;", linkageName: ">=", scope: !21, file: !21, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!21 = !DIFile(filename: "operators.jl", directory: ".")
!22 = !DILocation(line: 359, scope: !23, inlinedAt: !25)
!23 = distinct !DISubprogram(name: "unitrange_last;", linkageName: "unitrange_last", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!24 = !DIFile(filename: "range.jl", directory: ".")
!25 = !DILocation(line: 354, scope: !26, inlinedAt: !27)
!26 = distinct !DISubprogram(name: "UnitRange;", linkageName: "UnitRange", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!27 = !DILocation(line: 5, scope: !28, inlinedAt: !10)
!28 = distinct !DISubprogram(name: "Colon;", linkageName: "Colon", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!29 = !DILocation(line: 86, scope: !30, inlinedAt: !22)
!30 = distinct !DISubprogram(name: "-;", linkageName: "-", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!31 = !DILocation(line: 83, scope: !32, inlinedAt: !33)
!32 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DILocation(line: 378, scope: !34, inlinedAt: !35)
!34 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !21, file: !21, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!35 = !DILocation(line: 609, scope: !36, inlinedAt: !37)
!36 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!37 = !DILocation(line: 833, scope: !38, inlinedAt: !10)
!38 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !24, file: !24, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!39 = !DILocation(line: 82, scope: !5)
!40 = !DILocation(line: 83, scope: !5)
!41 = !DILocation(line: 87, scope: !42, inlinedAt: !40)
!42 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!43 = !DILocation(line: 862, scope: !44, inlinedAt: !40)
!44 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!45 = !{!46, !46, i64 0}
!46 = !{!"jtbaa_arraybuf", !47, i64 0}
!47 = !{!"jtbaa_data", !13, i64 0}
!48 = !DILocation(line: 905, scope: !49, inlinedAt: !40)
!49 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!50 = !DILocation(line: 468, scope: !51, inlinedAt: !53)
!51 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !52, file: !52, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!52 = !DIFile(filename: "promotion.jl", directory: ".")
!53 = !DILocation(line: 837, scope: !38, inlinedAt: !40)
!54 = !DILocation(line: 86, scope: !5)
!55 = !{i64 40}
!56 = !{i64 16}
!57 = !{i64 8}
