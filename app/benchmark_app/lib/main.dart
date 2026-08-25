import 'package:flutter/material.dart';

import 'src/app_shell.dart';

void main() {
  runApp(const BenchmarkApp());
}

class BenchmarkApp extends StatelessWidget {
  const BenchmarkApp({super.key});

  @override
  Widget build(BuildContext context) {
    const background = Color(0xFF111417);
    const surface = Color(0xFF1A1F24);
    const accent = Color(0xFF49B6A7);

    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'RapidBench',
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: background,
        colorScheme: const ColorScheme.dark(primary: accent, surface: surface),
        fontFamily: 'sans-serif',
        useMaterial3: false,
      ),
      home: const AppShell(),
    );
  }
}
