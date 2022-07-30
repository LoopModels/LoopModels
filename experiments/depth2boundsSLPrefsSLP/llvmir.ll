; ModuleID = './depth2boundsSLPrefsSLP/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @julia_foo_254({} addrspace(10)* nonnull readonly returned align 16 dereferenceable(40) %0, i64 signext %1, i64 signext %2, i64 signext %3, i64 signext %4, i64 signext %5, i64 signext %6) local_unnamed_addr #0 !dbg !5 {
top:
  %7 = tail call {}*** @julia.get_pgcstack()
  %8 = shl i64 %3, 1, !dbg !7
  %.not = icmp slt i64 %8, %1, !dbg !11
  %9 = add i64 %1, -1, !dbg !23
  %10 = select i1 %.not, i64 %9, i64 %8, !dbg !16
  %.not6 = icmp slt i64 %10, %1, !dbg !25
  br i1 %.not6, label %L64, label %L15.preheader, !dbg !10

L15.preheader:                                    ; preds = %top
  %11 = shl i64 %4, 2
  %12 = add i64 %11, -2
  %.not7 = icmp slt i64 %12, %2
  %13 = add i64 %2, -1
  %14 = select i1 %.not7, i64 %13, i64 %12
  %.not8 = icmp slt i64 %14, %2
  %15 = xor i64 %5, -1
  %16 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*
  %17 = addrspacecast {} addrspace(10)* addrspace(10)* %16 to {} addrspace(10)* addrspace(11)*
  %18 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %17, i64 3
  %19 = bitcast {} addrspace(10)* addrspace(11)* %18 to i64 addrspace(11)*
  %20 = load i64, i64 addrspace(11)* %19, align 8
  %21 = add i64 %6, -2
  %22 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %23 = addrspacecast i64 addrspace(13)* addrspace(10)* %22 to i64 addrspace(13)* addrspace(11)*
  %24 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %23, align 8
  br label %L15, !dbg !33

L15:                                              ; preds = %L15.preheader, %L52
  %value_phi3 = phi i64 [ %38, %L52 ], [ %1, %L15.preheader ]
  br i1 %.not8, label %L52, label %L32.preheader, !dbg !33

L32.preheader:                                    ; preds = %L15
  %25 = mul i64 %value_phi3, 3
  %26 = add i64 %25, %15
  %27 = add i64 %value_phi3, -1
  br label %L32, !dbg !34

L32:                                              ; preds = %L32.preheader, %L32
  %value_phi8 = phi i64 [ %37, %L32 ], [ %2, %L32.preheader ]
  %28 = add i64 %21, %value_phi8, !dbg !35
  %29 = mul i64 %28, %20, !dbg !35
  %30 = add i64 %26, %29, !dbg !35
  %31 = getelementptr inbounds i64, i64 addrspace(13)* %24, i64 %30, !dbg !35
  %32 = load i64, i64 addrspace(13)* %31, align 8, !dbg !35, !tbaa !38
  %33 = add i64 %value_phi8, -1, !dbg !43
  %34 = mul i64 %33, %20, !dbg !43
  %35 = add i64 %27, %34, !dbg !43
  %36 = getelementptr inbounds i64, i64 addrspace(13)* %24, i64 %35, !dbg !43
  store i64 %32, i64 addrspace(13)* %36, align 8, !dbg !43, !tbaa !38
  %.not9.not = icmp eq i64 %value_phi8, %14, !dbg !45
  %37 = add i64 %value_phi8, 1, !dbg !48
  br i1 %.not9.not, label %L52.loopexit, label %L32, !dbg !34

L52.loopexit:                                     ; preds = %L32
  br label %L52, !dbg !45

L52:                                              ; preds = %L52.loopexit, %L15
  %.not10 = icmp eq i64 %value_phi3, %10, !dbg !45
  %38 = add i64 %value_phi3, 1, !dbg !48
  br i1 %.not10, label %L64.loopexit, label %L15, !dbg !34

L64.loopexit:                                     ; preds = %L52
  br label %L64, !dbg !49

L64:                                              ; preds = %L64.loopexit, %top
  ret {} addrspace(10)* %0, !dbg !49
}

define nonnull {} addrspace(10)* @jfptr_foo_255({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
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
  %15 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 3
  %16 = bitcast {} addrspace(10)** %15 to i64 addrspace(10)**
  %17 = load i64 addrspace(10)*, i64 addrspace(10)** %16, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %18 = addrspacecast i64 addrspace(10)* %17 to i64 addrspace(11)*
  %19 = load i64, i64 addrspace(11)* %18, align 8
  %20 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 4
  %21 = bitcast {} addrspace(10)** %20 to i64 addrspace(10)**
  %22 = load i64 addrspace(10)*, i64 addrspace(10)** %21, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %23 = addrspacecast i64 addrspace(10)* %22 to i64 addrspace(11)*
  %24 = load i64, i64 addrspace(11)* %23, align 8
  %25 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 5
  %26 = bitcast {} addrspace(10)** %25 to i64 addrspace(10)**
  %27 = load i64 addrspace(10)*, i64 addrspace(10)** %26, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %28 = addrspacecast i64 addrspace(10)* %27 to i64 addrspace(11)*
  %29 = load i64, i64 addrspace(11)* %28, align 8
  %30 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 6
  %31 = bitcast {} addrspace(10)** %30 to i64 addrspace(10)**
  %32 = load i64 addrspace(10)*, i64 addrspace(10)** %31, align 8, !nonnull !4, !dereferenceable !52, !align !52
  %33 = addrspacecast i64 addrspace(10)* %32 to i64 addrspace(11)*
  %34 = load i64, i64 addrspace(11)* %33, align 8
  %35 = tail call nonnull {} addrspace(10)* @julia_foo_254({} addrspace(10)* %4, i64 signext %9, i64 signext %14, i64 signext %19, i64 signext %24, i64 signext %29, i64 signext %34) #0
  %36 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8
  ret {} addrspace(10)* %36
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth2boundsSLPrefsSLP/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "julia_foo_254", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 88, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "int.jl", directory: ".")
!10 = !DILocation(line: 4, scope: !5)
!11 = !DILocation(line: 477, scope: !12, inlinedAt: !13)
!12 = distinct !DISubprogram(name: "<=;", linkageName: "<=", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!13 = !DILocation(line: 425, scope: !14, inlinedAt: !16)
!14 = distinct !DISubprogram(name: ">=;", linkageName: ">=", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!15 = !DIFile(filename: "operators.jl", directory: ".")
!16 = !DILocation(line: 359, scope: !17, inlinedAt: !19)
!17 = distinct !DISubprogram(name: "unitrange_last;", linkageName: "unitrange_last", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!18 = !DIFile(filename: "range.jl", directory: ".")
!19 = !DILocation(line: 354, scope: !20, inlinedAt: !21)
!20 = distinct !DISubprogram(name: "UnitRange;", linkageName: "UnitRange", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!21 = !DILocation(line: 5, scope: !22, inlinedAt: !10)
!22 = distinct !DISubprogram(name: "Colon;", linkageName: "Colon", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!23 = !DILocation(line: 86, scope: !24, inlinedAt: !16)
!24 = distinct !DISubprogram(name: "-;", linkageName: "-", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!25 = !DILocation(line: 83, scope: !26, inlinedAt: !27)
!26 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!27 = !DILocation(line: 378, scope: !28, inlinedAt: !29)
!28 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!29 = !DILocation(line: 609, scope: !30, inlinedAt: !31)
!30 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!31 = !DILocation(line: 833, scope: !32, inlinedAt: !10)
!32 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !18, file: !18, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DILocation(line: 5, scope: !5)
!34 = !DILocation(line: 6, scope: !5)
!35 = !DILocation(line: 862, scope: !36, inlinedAt: !34)
!36 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !37, file: !37, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!37 = !DIFile(filename: "array.jl", directory: ".")
!38 = !{!39, !39, i64 0}
!39 = !{!"jtbaa_arraybuf", !40, i64 0}
!40 = !{!"jtbaa_data", !41, i64 0}
!41 = !{!"jtbaa", !42, i64 0}
!42 = !{!"jtbaa"}
!43 = !DILocation(line: 905, scope: !44, inlinedAt: !34)
!44 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !37, file: !37, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!45 = !DILocation(line: 468, scope: !46, inlinedAt: !48)
!46 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !47, file: !47, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!47 = !DIFile(filename: "promotion.jl", directory: ".")
!48 = !DILocation(line: 837, scope: !32, inlinedAt: !34)
!49 = !DILocation(line: 9, scope: !5)
!50 = !{i64 40}
!51 = !{i64 16}
!52 = !{i64 8}
