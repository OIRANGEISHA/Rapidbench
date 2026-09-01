import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

import 'device_info_service.dart';

class DevicePage extends StatefulWidget {
  const DevicePage({super.key});

  @override
  State<DevicePage> createState() => _DevicePageState();
}

class _DevicePageState extends State<DevicePage> {
  late Future<DeviceInfoBundle> _future;

  @override
  void initState() {
    super.initState();
    _future = DeviceInfoService.load();
  }

  Future<void> _refresh() async {
    final refreshed = DeviceInfoService.load();
    setState(() => _future = refreshed);
    await refreshed;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(elevation: 0, title: const Text('DEVICE')),
      body: FutureBuilder<DeviceInfoBundle>(
        future: _future,
        builder: (context, snapshot) {
          if (snapshot.hasError) {
            return _DeviceFailure(error: snapshot.error, onRetry: _refresh);
          }
          final bundle = snapshot.data;
          if (bundle == null) {
            return const Center(
                child: CircularProgressIndicator(strokeWidth: 2));
          }
          return RefreshIndicator(
            onRefresh: _refresh,
            child: _DeviceOverview(bundle: bundle),
          );
        },
      ),
    );
  }
}

class _DeviceOverview extends StatelessWidget {
  const _DeviceOverview({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final soc = bundle.section('soc');
    final system = bundle.section('system');
    final memory = bundle.section('memory');
    final storage = bundle.section('storage');
    final gpu = bundle.section('gpu');
    final topology = bundle.topology;
    final app = bundle.section('app');
    final cpuIsa = bundle.cpuIsa;
    final vulkanAvailable = bundle.vulkan['status'] == 'available';
    return ListView(
      physics: const AlwaysScrollableScrollPhysics(),
      padding: const EdgeInsets.fromLTRB(20, 16, 20, 28),
      children: [
        const Text(
          'HARDWARE & SOFTWARE',
          style: TextStyle(
            color: Color(0xFF8C989F),
            fontSize: 12,
            letterSpacing: 1.3,
          ),
        ),
        const SizedBox(height: 10),
        _DeviceSectionCard(
          icon: Icons.memory,
          title: 'CPU',
          lines: [
            _value(soc, 'model'),
            '${topology.cpus.length} logical cores • '
                '${_value(cpuIsa, 'architectureLevel')}',
          ],
          onTap: () => _open(context, _CpuDetailPage(bundle: bundle)),
        ),
        _DeviceSectionCard(
          icon: Icons.developer_board_outlined,
          title: 'GPU',
          lines: [
            _value(gpu, 'renderer'),
            '${_shortGlVersion(_value(gpu, 'openGlEsVersion'))} • '
                'Vulkan ${vulkanAvailable ? _value(bundle.vulkan, 'apiVersion') : 'Unavailable'}',
          ],
          onTap: () => _open(context, _GpuDetailPage(bundle: bundle)),
        ),
        _DeviceSectionCard(
          icon: Icons.view_stream_outlined,
          title: 'MEMORY',
          lines: [
            '${_bytes(memory['totalBytes'])} total • ${_value(memory, 'type')}',
            _memoryFrequency(bundle.memoryFrequency),
          ],
          onTap: () => _open(context, _MemoryDetailPage(bundle: bundle)),
        ),
        _DeviceSectionCard(
          icon: Icons.storage_outlined,
          title: 'STORAGE',
          lines: [
            '${_bytes(storage['totalBytes'])} total • ${_value(storage, 'type')}',
            '${_bytes(storage['availableBytes'])} available',
          ],
          onTap: () => _open(context, _StorageDetailPage(bundle: bundle)),
        ),
        _DeviceSectionCard(
          icon: Icons.phone_android_outlined,
          title: 'SYSTEM',
          lines: [
            '${_value(system, 'manufacturer')} ${_value(system, 'model')}',
            'Android ${_value(system, 'androidVersion')} • SDK ${_value(system, 'sdkLevel')}',
          ],
          onTap: () => _open(context, _SystemDetailPage(bundle: bundle)),
        ),
        _DeviceSectionCard(
          icon: Icons.info_outline,
          title: 'ABOUT APP',
          lines: [
            '${_value(app, 'name')} • Version '
                '${_value(app, 'versionName')}',
            _value(app, 'repository'),
          ],
          onTap: () => _open(context, _AboutAppPage(app: app)),
        ),
      ],
    );
  }

  static void _open(BuildContext context, Widget page) {
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => page));
  }
}

class _DeviceSectionCard extends StatelessWidget {
  const _DeviceSectionCard({
    required this.icon,
    required this.title,
    required this.lines,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final List<String> lines;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Material(
        color: const Color(0xFF1A1F24),
        shape: RoundedRectangleBorder(
          side: const BorderSide(color: Color(0xFF293139)),
          borderRadius: BorderRadius.circular(5),
        ),
        child: InkWell(
          borderRadius: BorderRadius.circular(5),
          onTap: onTap,
          child: Padding(
            padding: const EdgeInsets.fromLTRB(15, 14, 10, 14),
            child: Row(
              children: [
                Icon(icon, color: const Color(0xFF49B6A7), size: 25),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: const TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.7,
                        ),
                      ),
                      const SizedBox(height: 6),
                      for (final line in lines)
                        Padding(
                          padding: const EdgeInsets.only(top: 2),
                          child: Text(
                            line,
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              color: Color(0xFF8C989F),
                              fontSize: 12,
                              height: 1.25,
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
                const Icon(Icons.chevron_right, color: Color(0xFF657077)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _CpuDetailPage extends StatelessWidget {
  const _CpuDetailPage({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final soc = bundle.section('soc');
    final system = bundle.section('system');
    final cpuIsa = bundle.cpuIsa;
    final topology = bundle.topology;
    final instructionFeatures = _stringList(cpuIsa['features']);
    final groups = <int, List<CpuLogicalInfo>>{};
    for (final cpu in topology.cpus) {
      groups.putIfAbsent(cpu.performanceGroup, () => []).add(cpu);
    }
    final groupIds = groups.keys.toList()..sort();
    return _DetailPage(
      title: 'CPU',
      children: [
        _DetailRow(label: 'SoC Model', value: _value(soc, 'model')),
        _DetailRow(
            label: 'SoC Manufacturer', value: _value(soc, 'manufacturer')),
        _DetailRow(label: 'Model Source', value: _value(soc, 'modelSource')),
        _DetailRow(label: 'Logical Cores', value: '${topology.cpus.length}'),
        _DetailRow(
            label: 'Online / Allowed',
            value: '${topology.onlineCount} / ${topology.allowedCount}'),
        _DetailRow(label: 'Android ABI', value: _value(system, 'architecture')),
        _DetailRow(
            label: 'Instruction Set', value: _value(cpuIsa, 'architecture')),
        _DetailRow(
            label: 'ISA Level', value: _value(cpuIsa, 'architectureLevel')),
        _DetailRow(
            label: 'Supported ABIs',
            value: _listValue(system['supportedAbis'])),
        const _DetailRow(label: 'Cache Sizes', value: 'Unavailable'),
        const _DetailSectionTitle('INSTRUCTION SET EXTENSIONS'),
        _CpuInstructionSetCard(features: instructionFeatures),
        _DetailRow(label: 'Level Source', value: _value(cpuIsa, 'levelSource')),
        _DetailRow(
            label: 'Feature Source', value: _value(cpuIsa, 'featureSource')),
        if (_value(cpuIsa, 'levelNote') != 'Unavailable')
          _DetailNotice(_value(cpuIsa, 'levelNote')),
        const _DetailSectionTitle('CPU CLUSTERS'),
        for (final groupId in groupIds)
          _CpuClusterCard(groupId: groupId, cpus: groups[groupId]!),
        if (topology.performanceGroupsAreBestEffort)
          const _DetailNotice(
            'Cluster grouping is inferred from the frequency, capacity and '
            'topology data exposed by this kernel.',
          ),
      ],
    );
  }
}

class _CpuInstructionSetCard extends StatelessWidget {
  const _CpuInstructionSetCard({required this.features});

  final List<String> features;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1F24),
        border: Border.all(color: const Color(0xFF293139)),
        borderRadius: BorderRadius.circular(5),
      ),
      child: features.isEmpty
          ? const Text(
              'Unavailable',
              style: TextStyle(color: Color(0xFF78838A)),
            )
          : Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final feature in features)
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 8,
                      vertical: 5,
                    ),
                    decoration: BoxDecoration(
                      color: const Color(0xFF12332F),
                      border: Border.all(color: const Color(0xFF245E56)),
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Text(
                      feature,
                      style: const TextStyle(
                        color: Color(0xFF78D0C2),
                        fontSize: 11,
                        fontFeatures: [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
              ],
            ),
    );
  }
}

class _CpuClusterCard extends StatelessWidget {
  const _CpuClusterCard({required this.groupId, required this.cpus});

  final int groupId;
  final List<CpuLogicalInfo> cpus;

  @override
  Widget build(BuildContext context) {
    final sorted = [...cpus]
      ..sort((left, right) => left.logicalCpu.compareTo(right.logicalCpu));
    final maxKhz = sorted.fold<int>(
      0,
      (maximum, cpu) =>
          cpu.maxFrequencyKhz > maximum ? cpu.maxFrequencyKhz : maximum,
    );
    final capacity = sorted.fold<int>(
      0,
      (maximum, cpu) => cpu.capacity > maximum ? cpu.capacity : maximum,
    );
    final clusterIds = sorted.map((cpu) => cpu.clusterId).toSet().toList()
      ..sort();
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(13),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1F24),
        border: Border.all(color: const Color(0xFF293139)),
        borderRadius: BorderRadius.circular(5),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('G$groupId',
              style: const TextStyle(fontWeight: FontWeight.w700)),
          const SizedBox(height: 6),
          Text(
            'CPU ${sorted.map((cpu) => cpu.logicalCpu).join(', ')}',
            style: const TextStyle(color: Color(0xFFB7C0C5)),
          ),
          const SizedBox(height: 3),
          Text(
            '${maxKhz > 0 ? '${(maxKhz / 1000).round()} MHz max' : 'Frequency unavailable'}'
            ' • ${capacity > 0 ? 'capacity $capacity' : 'capacity unavailable'}'
            ' • cluster ${clusterIds.join(', ')}',
            style: const TextStyle(color: Color(0xFF78838A), fontSize: 11),
          ),
        ],
      ),
    );
  }
}

class _GpuDetailPage extends StatelessWidget {
  const _GpuDetailPage({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final gpu = bundle.section('gpu');
    final vulkan = bundle.vulkan;
    final vulkanAvailable = vulkan['status'] == 'available';
    return _DetailPage(
      title: 'GPU',
      children: [
        _DetailRow(label: 'Renderer', value: _value(gpu, 'renderer')),
        _DetailRow(label: 'Vendor', value: _value(gpu, 'vendor')),
        _DetailRow(label: 'OpenGL ES', value: _value(gpu, 'openGlEsVersion')),
        _DetailRow(label: 'GLSL', value: _value(gpu, 'glslVersion')),
        _DetailRow(
          label: 'Vulkan API',
          value: vulkanAvailable ? _value(vulkan, 'apiVersion') : 'Unavailable',
        ),
        _DetailRow(label: 'Vulkan Device', value: _value(vulkan, 'deviceName')),
        _DetailRow(
            label: 'Vulkan Driver', value: _value(vulkan, 'driverVersion')),
        _DetailRow(
            label: 'Vendor / Device ID',
            value:
                '${_value(vulkan, 'vendorId')} / ${_value(vulkan, 'deviceId')}'),
        const SizedBox(height: 12),
        OutlinedButton.icon(
          onPressed: () => Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) => VulkanFeaturesPage(vulkan: vulkan),
            ),
          ),
          icon: const Icon(Icons.list_alt_outlined),
          label: const Text('VULKAN FEATURES'),
        ),
        if (!vulkanAvailable) _DetailNotice(_value(vulkan, 'reason')),
      ],
    );
  }
}

class _MemoryDetailPage extends StatelessWidget {
  const _MemoryDetailPage({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final memory = bundle.section('memory');
    final total = _intValue(memory['totalBytes']);
    final available = _intValue(memory['availableBytes']);
    return _DetailPage(
      title: 'MEMORY',
      children: [
        _DetailRow(label: 'Total RAM', value: _bytes(total)),
        _DetailRow(label: 'Available RAM', value: _bytes(available)),
        _DetailRow(label: 'In Use', value: _bytes(total - available)),
        _DetailRow(label: 'Memory Type', value: _value(memory, 'type')),
        _DetailRow(
            label: 'DRAM Frequency',
            value: _memoryFrequency(bundle.memoryFrequency)),
        _DetailRow(
            label: 'Low-memory State',
            value: memory['lowMemory'] == true ? 'Active' : 'Normal'),
        const _DetailNotice(
          'Android does not expose RAM technology or DRAM clock consistently. '
          'Unavailable means the kernel did not provide a reliable value.',
        ),
      ],
    );
  }
}

class _StorageDetailPage extends StatelessWidget {
  const _StorageDetailPage({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final storage = bundle.section('storage');
    return _DetailPage(
      title: 'STORAGE',
      children: [
        _DetailRow(label: 'Total', value: _bytes(storage['totalBytes'])),
        _DetailRow(label: 'Used', value: _bytes(storage['usedBytes'])),
        _DetailRow(
            label: 'Available', value: _bytes(storage['availableBytes'])),
        _DetailRow(label: 'Storage Type', value: _value(storage, 'type')),
        _DetailRow(label: 'Measured Volume', value: _value(storage, 'scope')),
        const _DetailNotice(
          'Reported capacity is the internal data volume visible to this app. '
          'Interface generation is not guessed when sysfs does not expose it.',
        ),
      ],
    );
  }
}

class _SystemDetailPage extends StatelessWidget {
  const _SystemDetailPage({required this.bundle});

  final DeviceInfoBundle bundle;

  @override
  Widget build(BuildContext context) {
    final system = bundle.section('system');
    return _DetailPage(
      title: 'SYSTEM',
      children: [
        _DetailRow(
            label: 'Manufacturer', value: _value(system, 'manufacturer')),
        _DetailRow(label: 'Brand', value: _value(system, 'brand')),
        _DetailRow(label: 'Model', value: _value(system, 'model')),
        _DetailRow(label: 'Device', value: _value(system, 'device')),
        _DetailRow(
            label: 'Android Version', value: _value(system, 'androidVersion')),
        _DetailRow(label: 'SDK Level', value: _value(system, 'sdkLevel')),
        _DetailRow(
            label: 'Security Patch', value: _value(system, 'securityPatch')),
        _DetailRow(label: 'Build ID', value: _value(system, 'buildId')),
        _DetailRow(label: 'Hardware', value: _value(system, 'hardware')),
        _DetailRow(
            label: 'Architecture', value: _value(system, 'architecture')),
        _DetailRow(
            label: 'Supported ABIs',
            value: _listValue(system['supportedAbis'])),
      ],
    );
  }
}

class _AboutAppPage extends StatelessWidget {
  const _AboutAppPage({required this.app});

  final Map<String, dynamic> app;

  @override
  Widget build(BuildContext context) {
    final repository = _value(app, 'repository');
    return _DetailPage(
      title: 'ABOUT APP',
      children: [
        _DetailRow(
          label: 'Application',
          value: _value(app, 'name'),
        ),
        _DetailRow(label: 'Version', value: _value(app, 'versionName')),
        _DetailRow(label: 'Build', value: _value(app, 'versionCode')),
        const _DetailSectionTitle('PROJECT'),
        _ProjectLinkCard(url: repository),
      ],
    );
  }
}

class _ProjectLinkCard extends StatelessWidget {
  const _ProjectLinkCard({required this.url});

  final String url;

  Future<void> _open(BuildContext context) async {
    try {
      final opened = await DeviceInfoService.openProjectPage();
      if (!context.mounted) {
        return;
      }
      if (!opened) {
        throw StateError('Android did not open the project page');
      }
    } catch (_) {
      if (!context.mounted) {
        return;
      }
      ScaffoldMessenger.of(context)
        ..hideCurrentSnackBar()
        ..showSnackBar(
          const SnackBar(
            content: Text(
              'Unable to open GitHub. Copy the link and open it in a browser.',
            ),
          ),
        );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(13),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1F24),
        border: Border.all(color: const Color(0xFF293139)),
        borderRadius: BorderRadius.circular(5),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SelectableText(
            url,
            style: const TextStyle(color: Color(0xFF49B6A7), height: 1.35),
          ),
          const SizedBox(height: 12),
          SizedBox(
            width: double.infinity,
            child: OutlinedButton.icon(
              onPressed: () => _open(context),
              icon: const Icon(Icons.open_in_browser_outlined),
              label: const Text('OPEN GITHUB'),
            ),
          ),
        ],
      ),
    );
  }
}

class VulkanFeaturesPage extends StatefulWidget {
  const VulkanFeaturesPage({required this.vulkan, super.key});

  final Map<String, dynamic> vulkan;

  @override
  State<VulkanFeaturesPage> createState() => _VulkanFeaturesPageState();
}

class _VulkanFeaturesPageState extends State<VulkanFeaturesPage> {
  String _query = '';

  @override
  Widget build(BuildContext context) {
    final available = widget.vulkan['status'] == 'available';
    final rawFeatures = widget.vulkan['features'];
    final features = rawFeatures is Map
        ? Map<String, dynamic>.from(rawFeatures)
        : const <String, dynamic>{};
    final rawExtensions = widget.vulkan['extensions'];
    final extensions = rawExtensions is List
        ? (rawExtensions.map((value) => value.toString()).toList()..sort())
        : <String>[];
    final filtered = extensions
        .where((extension) =>
            extension.toLowerCase().contains(_query.toLowerCase()))
        .toList(growable: false);
    return Scaffold(
      appBar: AppBar(elevation: 0, title: const Text('VULKAN FEATURES')),
      body: !available
          ? Padding(
              padding: const EdgeInsets.all(20),
              child: _DetailNotice(_value(widget.vulkan, 'reason')),
            )
          : ListView(
              padding: const EdgeInsets.fromLTRB(20, 16, 20, 28),
              children: [
                _DetailRow(
                    label: 'API Version',
                    value: _value(widget.vulkan, 'apiVersion')),
                _DetailRow(
                    label: 'Driver Version',
                    value: _value(widget.vulkan, 'driverVersion')),
                const _DetailSectionTitle('QUERIED FEATURES'),
                for (final feature in features.entries)
                  _FeatureRow(
                      name: feature.key, supported: feature.value == true),
                const _DetailSectionTitle('DEVICE EXTENSIONS'),
                TextField(
                  onChanged: (value) => setState(() => _query = value),
                  decoration: const InputDecoration(
                    isDense: true,
                    prefixIcon: Icon(Icons.search, size: 20),
                    hintText: 'Search extensions',
                    border: OutlineInputBorder(),
                  ),
                ),
                const SizedBox(height: 10),
                Text(
                  '${filtered.length} of ${extensions.length}',
                  style:
                      const TextStyle(color: Color(0xFF78838A), fontSize: 11),
                ),
                const SizedBox(height: 6),
                for (final extension in filtered)
                  Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: SelectableText(
                      extension,
                      style: const TextStyle(
                        color: Color(0xFFB7C0C5),
                        fontSize: 12,
                        fontFeatures: [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
              ],
            ),
    );
  }
}

class _FeatureRow extends StatelessWidget {
  const _FeatureRow({required this.name, required this.supported});

  final String name;
  final bool supported;

  @override
  Widget build(BuildContext context) {
    final color = supported ? const Color(0xFF49B6A7) : const Color(0xFF78838A);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        children: [
          Expanded(child: Text(name)),
          const SizedBox(width: 12),
          Icon(
            supported ? Icons.check_circle_outline : Icons.cancel_outlined,
            color: color,
            size: 18,
          ),
          const SizedBox(width: 6),
          Text(supported ? 'Supported' : 'Unsupported',
              style: TextStyle(color: color, fontSize: 12)),
        ],
      ),
    );
  }
}

class _DetailPage extends StatelessWidget {
  const _DetailPage({required this.title, required this.children});

  final String title;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(elevation: 0, title: Text(title)),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(20, 14, 20, 28),
          children: children,
        ),
      ),
    );
  }
}

class _DetailRow extends StatelessWidget {
  const _DetailRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 126,
            child:
                Text(label, style: const TextStyle(color: Color(0xFF8C989F))),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Text(
              value,
              textAlign: TextAlign.right,
              style: const TextStyle(
                height: 1.35,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _DetailSectionTitle extends StatelessWidget {
  const _DetailSectionTitle(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 22, bottom: 10),
      child: Text(
        text,
        style: const TextStyle(
          color: Color(0xFF8C989F),
          fontSize: 12,
          letterSpacing: 1.3,
        ),
      ),
    );
  }
}

class _DetailNotice extends StatelessWidget {
  const _DetailNotice(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 12),
      child: Text(
        text,
        style: const TextStyle(
            color: Color(0xFFFFB74D), fontSize: 12, height: 1.4),
      ),
    );
  }
}

class _DeviceFailure extends StatelessWidget {
  const _DeviceFailure({required this.error, required this.onRetry});

  final Object? error;
  final Future<void> Function() onRetry;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Device information could not be loaded.'),
          const SizedBox(height: 8),
          Text(
            error?.toString() ?? 'Unknown error',
            style: const TextStyle(color: Color(0xFFFF8A80)),
          ),
          const SizedBox(height: 16),
          OutlinedButton(onPressed: onRetry, child: const Text('RETRY')),
        ],
      ),
    );
  }
}

String _value(Map<String, dynamic> source, String key) {
  final value = source[key];
  if (value == null || value.toString().trim().isEmpty) {
    return 'Unavailable';
  }
  return value.toString();
}

int _intValue(Object? value) => value is num ? value.toInt() : 0;

String _bytes(Object? value) {
  final bytes = _intValue(value);
  if (bytes <= 0) {
    return 'Unavailable';
  }
  final gib = bytes / (1024 * 1024 * 1024);
  return '${gib.toStringAsFixed(gib >= 100 ? 0 : 2)} GiB';
}

String _listValue(Object? value) {
  if (value is List && value.isNotEmpty) {
    return value.join(', ');
  }
  return 'Unavailable';
}

List<String> _stringList(Object? value) {
  if (value is! List) {
    return const [];
  }
  return value
      .map((entry) => entry.toString().trim())
      .where((entry) => entry.isNotEmpty)
      .toList(growable: false);
}

String _memoryFrequency(MemoryFrequencyInfo frequency) {
  if (!frequency.available) {
    return 'DRAM frequency unavailable';
  }
  final parts = <String>[];
  if (frequency.currentHz > 0) {
    parts.add('${(frequency.currentHz / 1000000).round()} MHz current');
  }
  if (frequency.maximumHz > 0) {
    parts.add('${(frequency.maximumHz / 1000000).round()} MHz max');
  }
  return parts.isEmpty ? 'DRAM frequency unavailable' : parts.join(' / ');
}

String _shortGlVersion(String version) {
  if (version == 'Unavailable') {
    return version;
  }
  final match = RegExp(r'OpenGL ES(?:-CM)?\s+[^\s]+').firstMatch(version);
  return match?.group(0) ?? version;
}
