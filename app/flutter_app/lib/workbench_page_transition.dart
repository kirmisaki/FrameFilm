import 'package:flutter/material.dart';

/// Animates the active destination without retaining an outgoing copy.
class WorkbenchPageTransition extends StatefulWidget {
  const WorkbenchPageTransition({
    super.key,
    required this.index,

    required this.child,
  });

  final int index;

  final Widget child;

  @override
  State<WorkbenchPageTransition> createState() =>
      _WorkbenchPageTransitionState();
}

class _WorkbenchPageTransitionState extends State<WorkbenchPageTransition>
    with SingleTickerProviderStateMixin {
  late final controller = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 220),
    value: 1,
  );
  late final animation = CurvedAnimation(
    parent: controller,
    curve: Curves.easeOutCubic,
  );

  @override
  void didUpdateWidget(covariant WorkbenchPageTransition oldWidget) {
    super.didUpdateWidget(oldWidget);

    if (oldWidget.index != widget.index) {
      if (MediaQuery.disableAnimationsOf(context)) {
        controller.value = 1;
      } else {
        controller.forward(from: 0);
      }
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (MediaQuery.disableAnimationsOf(context)) controller.value = 1;
  }

  @override
  void dispose() {
    animation.dispose();
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: animation,
    builder: (context, child) => Opacity(
      opacity: animation.value,
      child: Transform.translate(
        offset: Offset(0, 12 * (1 - animation.value)),
        child: child,
      ),
    ),
    child: widget.child,
  );
}
