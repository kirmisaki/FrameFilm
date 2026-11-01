import 'package:flutter/material.dart';

import 'forfilm_theme.dart';

// Shared geometry follows the existing ForFilm and Ark web styles.
class WorkbenchPanel extends StatelessWidget {
  const WorkbenchPanel({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(24),
    this.color,
  });
  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? color;
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final ark = theme.brightness == Brightness.dark;
    return Container(
      margin: EdgeInsets.only(right: ark ? 0 : 5, bottom: ark ? 0 : 5),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(ark ? 0 : 20),
        boxShadow: ark
            ? null
            : const [BoxShadow(color: ForFilmColors.ink, offset: Offset(5, 5))],
      ),
      child: Material(
        color: color ?? theme.colorScheme.surface,
        clipBehavior: Clip.antiAlias,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ark ? 0 : 20),
          side: BorderSide(
            color: theme.colorScheme.outline,
            width: ark ? 1 : 3,
          ),
        ),
        child: Padding(padding: padding, child: child),
      ),
    );
  }
}

class WorkbenchBrand extends StatelessWidget {
  const WorkbenchBrand({super.key, required this.ark});
  final bool ark;
  @override
  Widget build(BuildContext context) => Row(
    mainAxisSize: MainAxisSize.min,
    children: [
      if (ark) ...[
        const RhodesMark(width: 30, height: 36),
        const SizedBox(width: 10),
      ],
      Flexible(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              ark ? 'RHODES ISLAND' : 'ForFilm',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: ark ? 17 : 25,
                fontWeight: FontWeight.w800,
                letterSpacing: ark ? 1.5 : 0,
              ),
            ),
            if (ark)
              const Text(
                'PRTS / PERSONAL TERMINAL',
                style: TextStyle(
                  fontSize: 9,
                  letterSpacing: 1.2,
                  color: ForFilmColors.arkMuted,
                ),
              ),
          ],
        ),
      ),
    ],
  );
}

class PrtsConnectionIntro extends StatelessWidget {
  const PrtsConnectionIntro({super.key, required this.deviceName});
  final String deviceName;
  @override
  Widget build(BuildContext context) => ColoredBox(
    color: ForFilmColors.arkBackground,
    child: TweenAnimationBuilder<double>(
      tween: Tween(begin: 0, end: 1),
      duration: const Duration(milliseconds: 1400),
      builder: (context, progress, _) => Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 440),
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                SizedBox(
                  width: 150,
                  height: 150,
                  child: Stack(
                    alignment: Alignment.center,
                    children: [
                      Transform.rotate(
                        angle: progress * .8,
                        child: SizedBox(
                          width: 144,
                          height: 144,
                          child: CircularProgressIndicator(
                            value: progress,
                            strokeWidth: 1,
                            color: ForFilmColors.arkLine,
                          ),
                        ),
                      ),
                      Opacity(
                        opacity: progress.clamp(0.0, 1.0),
                        child: const RhodesMark(width: 104, height: 118),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 28),
                const Text(
                  'ARKNIGHTS',
                  style: TextStyle(
                    color: ForFilmColors.arkInk,
                    fontSize: 28,
                    letterSpacing: 5,
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const SizedBox(height: 8),
                const Text(
                  'PRTS · SYNCHRONIZING',
                  style: TextStyle(
                    color: ForFilmColors.arkMuted,
                    fontSize: 11,
                    letterSpacing: 2,
                  ),
                ),
                const SizedBox(height: 32),
                LinearProgressIndicator(
                  value: progress,
                  minHeight: 2,
                  color: ForFilmColors.arkInk,
                  backgroundColor: ForFilmColors.arkLine,
                ),
                const SizedBox(height: 12),
                Text(
                  deviceName,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: ForFilmColors.arkMuted),
                ),
                const SizedBox(height: 6),
                const Text(
                  '连接已建立 · 正在进入工作台',
                  style: TextStyle(color: ForFilmColors.arkMuted, fontSize: 11),
                ),
              ],
            ),
          ),
        ),
      ),
    ),
  );
}

class RhodesMark extends StatelessWidget {
  const RhodesMark({super.key, required this.width, required this.height});
  final double width, height;
  @override
  Widget build(BuildContext context) => ColorFiltered(
    colorFilter: const ColorFilter.matrix([
      0,
      0,
      0,
      0,
      240,
      0,
      0,
      0,
      0,
      241,
      0,
      0,
      0,
      0,
      242,
      -1,
      0,
      0,
      0,
      255,
    ]),
    child: Image.asset('assets/rhodes_logo.png', width: width, height: height),
  );
}

class WorkbenchNavigation extends StatelessWidget {
  const WorkbenchNavigation({
    super.key,
    required this.items,
    required this.selected,
    required this.onSelected,
  });
  final List<(String, IconData)> items;
  final int selected;
  final ValueChanged<int> onSelected;
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context), scheme = Theme.of(context).colorScheme;
    final ark = theme.brightness == Brightness.dark;
    return SizedBox(
      width: 200,
      child: Align(
        alignment: Alignment.topCenter,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 32, 8, 16),
          child: WorkbenchPanel(
            padding: const EdgeInsets.all(8),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                for (var index = 0; index < items.length; index++)
                  Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Semantics(
                      button: true,
                      selected: index == selected,
                      onTap: () => onSelected(index),
                      label:
                          '${items[index].$1} Tab ${index + 1} of ${items.length}',
                      excludeSemantics: true,
                      child: Material(
                        color: index == selected
                            ? scheme.primary
                            : Colors.transparent,
                        borderRadius: BorderRadius.circular(ark ? 0 : 12),
                        child: InkWell(
                          onTap: () => onSelected(index),
                          borderRadius: BorderRadius.circular(ark ? 0 : 12),
                          child: Padding(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 16,
                              vertical: 15,
                            ),
                            child: Row(
                              children: [
                                Icon(
                                  items[index].$2,
                                  size: 20,
                                  color: index == selected
                                      ? scheme.onPrimary
                                      : scheme.onSurfaceVariant,
                                ),
                                const SizedBox(width: 12),
                                Text(
                                  items[index].$1,
                                  style: TextStyle(
                                    fontWeight: FontWeight.w600,
                                    color: index == selected
                                        ? scheme.onPrimary
                                        : scheme.onSurfaceVariant,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
