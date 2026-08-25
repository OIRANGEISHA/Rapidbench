import 'package:flutter/material.dart';

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

class _AppShellState extends State<AppShell> {
  int _selectedIndex = 0;

  static const _pages = <Widget>[
    CpuBenchPage(),
    MemoryBenchPage(),
    StorageBenchPage(),
    GpuBenchPage(),
    DevicePage(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(index: _selectedIndex, children: _pages),
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
