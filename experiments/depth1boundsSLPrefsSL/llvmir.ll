; ModuleID = './depth1boundsSLPrefsSL/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @julia_foo_105({} addrspace(10)* nonnull readonly returned align 16 dereferenceable(40) %0, i64 signext %1, i64 signext %2) local_unnamed_addr #0 !dbg !5 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %.not = icmp sgt i64 %1, %2, !dbg !7
  %4 = add i64 %1, -1, !dbg !21
  %5 = select i1 %.not, i64 %4, i64 %2, !dbg !13
  %.not4 = icmp slt i64 %5, %1, !dbg !23
  br i1 %.not4, label %L31, label %L14.preheader, !dbg !20

L14.preheader:                                    ; preds = %top
  %6 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(10)*
  %7 = addrspacecast {} addrspace(10)* addrspace(10)* %6 to {} addrspace(10)* addrspace(11)*
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 3
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*
  %10 = load i64, i64 addrspace(11)* %9, align 8, !tbaa !31, !range !35, !invariant.load !4
  %11 = bitcast {} addrspace(10)* %0 to i64 addrspace(13)* addrspace(10)*
  %12 = addrspacecast i64 addrspace(13)* addrspace(10)* %11 to i64 addrspace(13)* addrspace(11)*
  %13 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %12, align 8, !tbaa !31, !invariant.load !4, !nonnull !4
  br label %L14, !dbg !36

L14:                                              ; preds = %L14.preheader, %L14
  %value_phi3 = phi i64 [ %20, %L14 ], [ %1, %L14.preheader ]
  %14 = add i64 %value_phi3, -1, !dbg !37
  %15 = mul i64 %14, %10, !dbg !37
  %16 = add i64 %15, %14, !dbg !37
  %17 = getelementptr inbounds i64, i64 addrspace(13)* %13, i64 %16, !dbg !37
  %18 = load i64, i64 addrspace(13)* %17, align 8, !dbg !37, !tbaa !40
  %19 = mul i64 %18, 44, !dbg !43
  store i64 %19, i64 addrspace(13)* %17, align 8, !dbg !45, !tbaa !40
  %.not5.not = icmp eq i64 %value_phi3, %5, !dbg !47
  %20 = add i64 %value_phi3, 1, !dbg !50
  br i1 %.not5.not, label %L31.loopexit, label %L14, !dbg !36

L31.loopexit:                                     ; preds = %L14
  br label %L31, !dbg !51

L31:                                              ; preds = %L31.loopexit, %top
  ret {} addrspace(10)* %0, !dbg !51
}

define nonnull {} addrspace(10)* @jfptr_foo_106({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !52, !align !53
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !54, !align !54
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  %10 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 2
  %11 = bitcast {} addrspace(10)** %10 to i64 addrspace(10)**
  %12 = load i64 addrspace(10)*, i64 addrspace(10)** %11, align 8, !nonnull !4, !dereferenceable !54, !align !54
  %13 = addrspacecast i64 addrspace(10)* %12 to i64 addrspace(11)*
  %14 = load i64, i64 addrspace(11)* %13, align 8
  %15 = tail call nonnull {} addrspace(10)* @julia_foo_105({} addrspace(10)* %4, i64 signext %9, i64 signext %14) #0
  %16 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8
  ret {} addrspace(10)* %16
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth1boundsSLPrefsSL/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "julia_foo_105", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
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
!20 = !DILocation(line: 4, scope: !5)
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
!31 = !{!32, !32, i64 0, i64 1}
!32 = !{!"jtbaa_const", !33, i64 0}
!33 = !{!"jtbaa", !34, i64 0}
!34 = !{!"jtbaa"}
!35 = !{i64 0, i64 9223372036854775807}
!36 = !DILocation(line: 5, scope: !5)
!37 = !DILocation(line: 862, scope: !38, inlinedAt: !36)
!38 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !39, file: !39, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!39 = !DIFile(filename: "array.jl", directory: ".")
!40 = !{!41, !41, i64 0}
!41 = !{!"jtbaa_arraybuf", !42, i64 0}
!42 = !{!"jtbaa_data", !33, i64 0}
!43 = !DILocation(line: 88, scope: !44, inlinedAt: !36)
!44 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!45 = !DILocation(line: 905, scope: !46, inlinedAt: !36)
!46 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !39, file: !39, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!47 = !DILocation(line: 468, scope: !48, inlinedAt: !50)
!48 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !49, file: !49, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!49 = !DIFile(filename: "promotion.jl", directory: ".")
!50 = !DILocation(line: 837, scope: !30, inlinedAt: !36)
!51 = !DILocation(line: 7, scope: !5)
!52 = !{i64 40}
!53 = !{i64 16}
!54 = !{i64 8}
