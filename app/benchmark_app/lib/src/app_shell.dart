import 'package:flutter/material.dart';
import 'package:benchmark_native/benchmark_native.dart';

import 'cpu_bench_page.dart';
import 'device_page.dart';
import 'gpu_bench_page.dart';
import 'memory_bench_page.dart';
import 'storage_bench_page.dart';

class AppShell extends StatefulWidget {
  const AppShell({super.key});

  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell> with WidgetsBindingObserver {
  int _selectedIndex = 0;

  static const _pages = <Widget>[
    CpuBenchPage(),
    MemoryBenchPage(),
    StorageBenchPage(),
    GpuBenchPage(),
    DevicePage(),
  ];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    BenchmarkRunCoordinator.instance.setForeground(
      WidgetsBinding.instance.lifecycleState != AppLifecycleState.paused &&
          WidgetsBinding.instance.lifecycleState != AppLifecycleState.hidden,
    );
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      BenchmarkRunCoordinator.instance.setForeground(true);
    } else if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached) {
      BenchmarkRunCoordinator.instance.setForeground(false);
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
        children: [
          Expanded(
              child: IndexedStack(index: _selectedIndex, children: _pages)),
          AnimatedBuilder(
            animation: BenchmarkRunCoordinator.instance,
            builder: (context, _) {
              final module = BenchmarkRunCoordinator.instance.activeModule;
              if (module == null || module.index == _selectedIndex) {
                return const SizedBox.shrink();
              }
              return Padding(
                padding:
                    const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                child: Text(
                  '${module.label} test running • finish it to start another test',
                  textAlign: TextAlign.center,
                  style:
                      const TextStyle(color: Color(0xFF49B6A7), fontSize: 12),
                ),
              );
            },
          ),
        ],
      ),
      bottomNavigationBar: _BenchmarkNavigationBar(
        selectedIndex: _selectedIndex,
        onSelected: (index) => setState(() => _selectedIndex = index),
      ),
    );
  }
}

class _BenchmarkNavigationBar extends StatelessWidget {
  const _BenchmarkNavigationBar({
    required this.selectedIndex,
    required this.onSelected,
  });

  final int selectedIndex;
  final ValueChanged<int> onSelected;

  static const _items = <({IconData icon, String label})>[
    (icon: Icons.memory, label: 'CPU'),
    (icon: Icons.view_stream_outlined, label: 'MEMORY'),
    (icon: Icons.storage_outlined, label: 'STORAGE'),
    (icon: Icons.developer_board_outlined, label: 'GPU'),
    (icon: Icons.phone_android_outlined, label: 'DEVICE'),
  ];

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.paddingOf(context).bottom;
    return DecoratedBox(
      decoration: const BoxDecoration(
        color: Color(0xFF15191D),
        border: Border(top: BorderSide(color: Color(0xFF293139))),
      ),
      child: SizedBox(
        height: 62 + bottomInset,
        child: Padding(
          padding: EdgeInsets.only(bottom: bottomInset),
          child: Row(
            children: List.generate(_items.length, (index) {
              final item = _items[index];
              final selected = index == selectedIndex;
              final color =
                  selected ? const Color(0xFF49B6A7) : const Color(0xFF78838A);
              return Expanded(
                child: InkWell(
                  onTap: () => onSelected(index),
                  child: Semantics(
                    selected: selected,
                    label: item.label,
                    button: true,
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(item.icon, color: color, size: 22),
                        const SizedBox(height: 4),
                        Text(
                          item.label,
                          maxLines: 1,
                          style: TextStyle(
                            color: color,
                            fontSize: 10,
                            fontWeight:
                                selected ? FontWeight.w700 : FontWeight.w500,
                            letterSpacing: 0.15,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              );
            }),
          ),
        ),
      ),
    );
  }
}
