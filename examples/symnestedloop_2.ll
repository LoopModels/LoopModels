; ModuleID = 'symnestedloop_2.ll'
source_filename = "symnestedloop_2!"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @"julia_symnestedloop_2!_137"({} addrspace(10)* nonnull readonly returned align 16 dereferenceable(40) %0, i64 signext %1, i64 signext %2, i64 signext %3, i64 signext %4, i64 signext %5, i64 signext %6) local_unnamed_addr #0 !dbg !5 {
top:
  %7 = tail call {}*** @julia.get_pgcstack()
  %.not = icmp sgt i64 %1, %4, !dbg !7
  %8 = add i64 %1, -1, !dbg !21
  %9 = select i1 %.not, i64 %8, i64 %4, !dbg !13
  %.not6 = icmp slt i64 %9, %1, !dbg !23
  br i1 %.not6, label %L59, label %L14.preheader, !dbg !20

L14.preheader:                                    ; preds = %top
  %.not7 = icmp sgt i64 %2, %4
  %10 = add i64 %2, -1
  %11 = select i1 %.not7, i64 %10, i64 %4
  %.not8 = icmp slt i64 %11, %2
  %12 = xor i64 %5, -1
  %13 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*
  %14 = addrspacecast {} addrspace(10)* addrspace(10)* %13 to {} addrspace(10)* addrspace(11)*
  %15 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %14, i64 3
  %16 = bitcast {} addrspace(10)* addrspace(11)* %15 to i64 addrspace(11)*
  %17 = load i64, i64 addrspace(11)* %16, align 8
  %18 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %19 = addrspacecast i64 addrspace(13)* addrspace(10)* %18 to i64 addrspace(13)* addrspace(11)*
  %20 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %19, align 8
  br label %L14, !dbg !31

L14:                                              ; preds = %L14.preheader, %L47
  %value_phi3 = phi i64 [ %33, %L47 ], [ %1, %L14.preheader ]
  br i1 %.not8, label %L47, label %L29.preheader, !dbg !31

L29.preheader:                                    ; preds = %L14
  %21 = add i64 %value_phi3, %12
  %22 = add i64 %value_phi3, -1
  br label %L29, !dbg !32

L29:                                              ; preds = %L29.preheader, %L29
  %value_phi8 = phi i64 [ %32, %L29 ], [ %2, %L29.preheader ]
  %23 = add i64 %value_phi8, -1, !dbg !33
  %24 = add i64 %23, %6, !dbg !35
  %25 = mul i64 %24, %17, !dbg !35
  %26 = add i64 %21, %25, !dbg !35
  %27 = getelementptr inbounds i64, i64 addrspace(13)* %20, i64 %26, !dbg !35
  %28 = load i64, i64 addrspace(13)* %27, align 8, !dbg !35, !tbaa !38
  %29 = mul i64 %23, %17, !dbg !43
  %30 = add i64 %22, %29, !dbg !43
  %31 = getelementptr inbounds i64, i64 addrspace(13)* %20, i64 %30, !dbg !43
  store i64 %28, i64 addrspace(13)* %31, align 8, !dbg !43, !tbaa !38
  %.not9.not = icmp eq i64 %value_phi8, %11, !dbg !45
  %32 = add i64 %value_phi8, 1, !dbg !48
  br i1 %.not9.not, label %L47.loopexit, label %L29, !dbg !32

L47.loopexit:                                     ; preds = %L29
  br label %L47, !dbg !45

L47:                                              ; preds = %L47.loopexit, %L14
  %.not10 = icmp eq i64 %value_phi3, %9, !dbg !45
  %33 = add i64 %value_phi3, 1, !dbg !48
  br i1 %.not10, label %L59.loopexit, label %L14, !dbg !32

L59.loopexit:                                     ; preds = %L47
  br label %L59, !dbg !49

L59:                                              ; preds = %L59.loopexit, %top
  ret {} addrspace(10)* %0, !dbg !49
}

define nonnull {} addrspace(10)* @"jfptr_symnestedloop_2!_138"({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !50, !align !51
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  %10 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 2
  %11 = bitcast {} addrspace(10)** %10 to i64 addrspace(10)**
  %12 = load i64 addrspace(10)*, i64 addrspace(10)** %11, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %13 = addrspacecast i64 addrspace(10)* %12 to i64 addrspace(11)*
  %14 = load i64, i64 addrspace(11)* %13, align 8
  %15 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 4
  %16 = bitcast {} addrspace(10)** %15 to i64 addrspace(10)**
  %17 = load i64 addrspace(10)*, i64 addrspace(10)** %16, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %18 = addrspacecast i64 addrspace(10)* %17 to i64 addrspace(11)*
  %19 = load i64, i64 addrspace(11)* %18, align 8
  %20 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 5
  %21 = bitcast {} addrspace(10)** %20 to i64 addrspace(10)**
  %22 = load i64 addrspace(10)*, i64 addrspace(10)** %21, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %23 = addrspacecast i64 addrspace(10)* %22 to i64 addrspace(11)*
  %24 = load i64, i64 addrspace(11)* %23, align 8
  %25 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 6
  %26 = bitcast {} addrspace(10)** %25 to i64 addrspace(10)**
  %27 = load i64 addrspace(10)*, i64 addrspace(10)** %26, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %28 = addrspacecast i64 addrspace(10)* %27 to i64 addrspace(11)*
  %29 = load i64, i64 addrspace(11)* %28, align 8
  %30 = tail call nonnull {} addrspace(10)* @"julia_symnestedloop_2!_137"({} addrspace(10)* %4, i64 signext %9, i64 signext %14, i64 signext poison, i64 signext %19, i64 signext %24, i64 signext %29) #0
  %31 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8
  ret {} addrspace(10)* %31
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
!5 = distinct !DISubprogram(name: "symnestedloop_2!", linkageName: "julia_symnestedloop_2!_137", scope: null, file: !3, line: 89, type: !6, scopeLine: 89, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 477, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "<=;", linkageName: "<=", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "int.jl", directory: ".")
!10 = !DILocation(line: 425, scope: !11, inlinedAt: !13)
!11 = distinct !DISubprogram(name: ">=;", linkageName: ">=", scope: !12, file: !12, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!12 = !DIFile(filename: "operators.jl", directory: ".")
!13 = !DILocation(line: 359, scope: !14, inlinedAt: !16)
!14 = distinct !DISubprogram(name: "unitrange_last;", linkageName: "unitrange_last", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!15 = !DIFile(filename: "range.jl", directory: ".")
!16 = !DILocation(line: 354, scope: !17, inlinedAt: !18)
!17 = distinct !DISubprogram(name: "UnitRange;", linkageName: "UnitRange", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!18 = !DILocation(line: 5, scope: !19, inlinedAt: !20)
!19 = distinct !DISubprogram(name: "Colon;", linkageName: "Colon", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!20 = !DILocation(line: 90, scope: !5)
!21 = !DILocation(line: 86, scope: !22, inlinedAt: !13)
!22 = distinct !DISubprogram(name: "-;", linkageName: "-", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!23 = !DILocation(line: 83, scope: !24, inlinedAt: !25)
!24 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!25 = !DILocation(line: 378, scope: !26, inlinedAt: !27)
!26 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !12, file: !12, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!27 = !DILocation(line: 609, scope: !28, inlinedAt: !29)
!28 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!29 = !DILocation(line: 833, scope: !30, inlinedAt: !20)
!30 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!31 = !DILocation(line: 91, scope: !5)
!32 = !DILocation(line: 92, scope: !5)
!33 = !DILocation(line: 87, scope: !34, inlinedAt: !32)
!34 = distinct !DISubprogram(name: "+;", linkageName: "+", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!35 = !DILocation(line: 862, scope: !36, inlinedAt: !32)
!36 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !37, file: !37, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!37 = !DIFile(filename: "array.jl", directory: ".")
!38 = !{!39, !39, i64 0}
!39 = !{!"jtbaa_arraybuf", !40, i64 0}
!40 = !{!"jtbaa_data", !41, i64 0}
!41 = !{!"jtbaa", !42, i64 0}
!42 = !{!"jtbaa"}
!43 = !DILocation(line: 905, scope: !44, inlinedAt: !32)
!44 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !37, file: !37, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!45 = !DILocation(line: 468, scope: !46, inlinedAt: !48)
!46 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !47, file: !47, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!47 = !DIFile(filename: "promotion.jl", directory: ".")
!48 = !DILocation(line: 837, scope: !30, inlinedAt: !32)
!49 = !DILocation(line: 95, scope: !5)
!50 = !{i64 40}
!51 = !{i64 16}
!52 = !{i64 8}
