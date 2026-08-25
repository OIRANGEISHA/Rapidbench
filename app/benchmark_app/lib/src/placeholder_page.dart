import 'package:flutter/material.dart';

class BenchmarkPlaceholderPage extends StatelessWidget {
  const BenchmarkPlaceholderPage({required this.title, super.key});

  final String title;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(elevation: 0, title: Text(title)),
      body: const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.construction_outlined,
                size: 36, color: Color(0xFF49B6A7)),
            SizedBox(height: 14),
            Text(
              'Coming Soon',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600),
            ),
            SizedBox(height: 6),
            Text(
              'This benchmark is under development.',
              style: TextStyle(color: Color(0xFF8C989F)),
            ),
          ],
        ),
      ),
    );
  }
}
