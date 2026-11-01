import 'package:flutter/material.dart';

/// Measured design tokens from tools/ForFilm/css/{style,ark-theme}.css.
abstract final class ForFilmColors {
  static const cream = Color(0xfffff8e7);
  static const creamSoft = Color(0xfffffdf6);
  static const ink = Color(0xff12100e);
  static const inkSoft = Color(0xff5b544a);
  static const lemon = Color(0xffd8f14a);
  static const coral = Color(0xffff6b5a);
  static const sky = Color(0xff4fb3ff);
  static const mint = Color(0xff3ed6a8);
  static const sun = Color(0xffffd84d);
  static const arkBackground = Color(0xff151719);
  static const arkSurface = Color(0xff222629);
  static const arkRaised = Color(0xff2c3135);
  static const arkInk = Color(0xfff0f1f2);
  static const arkMuted = Color(0xffadb5bb);
  static const arkHint = Color(0xff8e999f);
  static const arkLine = Color(0xff41484d);
  static const arkAccent = Color(0xff83afc1);
  static const arkError = Color(0xffe58d89);
  static const arkSuccess = Color(0xffa5c5b1);
  static const arkWarning = Color(0xffd5bd8e);
}

/// Both web themes use their own geometry, including flat square Ark surfaces.
/// Keep the platform font fallback: the web display fonts are not bundled.
ThemeData buildForFilmTheme(bool ark) {
  final background = ark ? ForFilmColors.arkBackground : ForFilmColors.cream;
  final surface = ark ? ForFilmColors.arkSurface : Colors.white;
  final ink = ark ? ForFilmColors.arkInk : ForFilmColors.ink;
  final muted = ark ? ForFilmColors.arkMuted : ForFilmColors.inkSoft;
  final line = ark ? ForFilmColors.arkLine : ForFilmColors.ink;
  final accent = ark ? ForFilmColors.arkAccent : ForFilmColors.sky;
  final primary = ark ? ForFilmColors.arkInk : ForFilmColors.lemon;
  final onPrimary = ark ? ForFilmColors.arkBackground : ForFilmColors.ink;
  final width = ark ? 1.0 : 3.0;
  final side = BorderSide(color: line, width: width);
  final controlRadius = BorderRadius.circular(ark ? 0 : 12);
  final cardRadius = BorderRadius.circular(ark ? 0 : 20);
  final shape = RoundedRectangleBorder(borderRadius: controlRadius, side: side);
  final brightness = ark ? Brightness.dark : Brightness.light;
  final scheme =
      ColorScheme.fromSeed(seedColor: primary, brightness: brightness).copyWith(
        primary: primary,
        onPrimary: onPrimary,
        primaryContainer: ark ? ForFilmColors.arkRaised : ForFilmColors.lemon,
        onPrimaryContainer: ink,
        secondary: ark ? ForFilmColors.arkAccent : ForFilmColors.mint,
        onSecondary: onPrimary,
        secondaryContainer: ark ? ForFilmColors.arkAccent : ForFilmColors.lemon,
        onSecondaryContainer: onPrimary,
        tertiary: ark ? ForFilmColors.arkWarning : ForFilmColors.sun,
        onTertiary: onPrimary,
        surface: surface,
        onSurface: ink,
        surfaceContainerLowest: background,
        surfaceContainerLow: surface,
        surfaceContainer: ark
            ? ForFilmColors.arkRaised
            : ForFilmColors.creamSoft,
        surfaceContainerHigh: ark
            ? ForFilmColors.arkRaised
            : ForFilmColors.creamSoft,
        surfaceContainerHighest: ark
            ? ForFilmColors.arkRaised
            : ForFilmColors.creamSoft,
        onSurfaceVariant: muted,
        outline: line,
        outlineVariant: line,
        error: ark ? ForFilmColors.arkError : const Color(0xffb8342b),
        onError: onPrimary,
        shadow: ark ? Colors.transparent : ForFilmColors.ink,
        surfaceTint: Colors.transparent,
      );
  final base = ThemeData(useMaterial3: true, colorScheme: scheme);
  final typography = base.textTheme
      .apply(bodyColor: ink, displayColor: ink)
      .copyWith(
        headlineLarge: TextStyle(
          fontSize: ark ? 32 : 30,
          height: 1.2,
          fontWeight: FontWeight.w600,
          letterSpacing: ark ? 1.2 : .2,
          color: ink,
        ),
        headlineMedium: TextStyle(
          fontSize: ark ? 28 : 26,
          height: 1.25,
          fontWeight: FontWeight.w600,
          color: ink,
        ),
        headlineSmall: TextStyle(
          fontSize: 23,
          height: 1.25,
          fontWeight: FontWeight.w600,
          color: ink,
        ),
        titleLarge: TextStyle(
          fontSize: ark ? 19 : 20,
          height: 1.35,
          fontWeight: FontWeight.w600,
          letterSpacing: ark ? .7 : 0,
          color: ink,
        ),
        titleMedium: TextStyle(
          fontSize: 16,
          height: 1.4,
          fontWeight: FontWeight.w600,
          color: ink,
        ),
        titleSmall: TextStyle(
          fontSize: 14,
          height: 1.4,
          fontWeight: FontWeight.w600,
          color: ink,
        ),
        bodyLarge: TextStyle(fontSize: 16, height: 1.55, color: ink),
        bodyMedium: TextStyle(fontSize: 14, height: 1.55, color: ink),
        bodySmall: TextStyle(fontSize: 12, height: 1.5, color: muted),
        labelLarge: TextStyle(
          fontSize: ark ? 13 : 14,
          height: 1.4,
          fontWeight: FontWeight.w600,
          letterSpacing: ark ? .6 : 0,
          color: ink,
        ),
        labelMedium: TextStyle(
          fontSize: 12,
          height: 1.4,
          fontWeight: FontWeight.w600,
          color: muted,
        ),
        labelSmall: TextStyle(
          fontSize: 11,
          height: 1.4,
          fontWeight: FontWeight.w600,
          letterSpacing: ark ? .4 : 0,
          color: muted,
        ),
      );
  ButtonStyle button(Color fill, Color foreground) => ButtonStyle(
    minimumSize: const WidgetStatePropertyAll(Size(44, 44)),
    padding: const WidgetStatePropertyAll(
      EdgeInsets.symmetric(horizontal: 16, vertical: 11),
    ),
    textStyle: WidgetStatePropertyAll(typography.labelLarge),
    shape: WidgetStatePropertyAll(shape),
    side: WidgetStatePropertyAll(
      ark && fill == primary ? BorderSide(color: ink) : side,
    ),
    elevation: const WidgetStatePropertyAll(0),
    surfaceTintColor: const WidgetStatePropertyAll(Colors.transparent),
    backgroundColor: WidgetStateProperty.resolveWith((states) {
      if (states.contains(WidgetState.disabled)) {
        return fill.withValues(alpha: ark ? .35 : .45);
      }
      if (ark &&
          (states.contains(WidgetState.hovered) ||
              states.contains(WidgetState.pressed))) {
        return ForFilmColors.arkRaised;
      }
      return fill;
    }),
    foregroundColor: WidgetStateProperty.resolveWith((states) {
      if (states.contains(WidgetState.disabled)) {
        return foreground.withValues(alpha: ark ? .35 : .45);
      }
      if (ark &&
          (states.contains(WidgetState.hovered) ||
              states.contains(WidgetState.pressed))) {
        return ink;
      }
      return foreground;
    }),
    overlayColor: WidgetStateProperty.resolveWith(
      (states) => states.contains(WidgetState.focused)
          ? accent.withValues(alpha: .2)
          : ink.withValues(alpha: .06),
    ),
  );
  final fieldBorder = OutlineInputBorder(
    borderRadius: controlRadius,
    borderSide: side,
  );
  return base.copyWith(
    scaffoldBackgroundColor: background,
    canvasColor: background,
    textTheme: typography,
    primaryTextTheme: typography,
    dividerColor: line,
    disabledColor: muted.withValues(alpha: .45),
    hoverColor: ark ? ForFilmColors.arkRaised : ForFilmColors.creamSoft,
    focusColor: accent.withValues(alpha: .2),
    iconTheme: IconThemeData(color: ink, size: 20),
    appBarTheme: AppBarTheme(
      backgroundColor: background,
      foregroundColor: ink,
      elevation: 0,
      scrolledUnderElevation: 0,
      surfaceTintColor: Colors.transparent,
      centerTitle: false,
      titleTextStyle: typography.headlineSmall,
    ),
    cardTheme: CardThemeData(
      color: surface,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(borderRadius: cardRadius, side: side),
      clipBehavior: Clip.antiAlias,
    ),
    filledButtonTheme: FilledButtonThemeData(style: button(primary, onPrimary)),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: button(primary, onPrimary),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: button(ark ? Colors.transparent : Colors.white, ink),
    ),
    textButtonTheme: TextButtonThemeData(
      style: ButtonStyle(
        foregroundColor: WidgetStatePropertyAll(ink),
        textStyle: WidgetStatePropertyAll(typography.labelLarge),
        shape: WidgetStatePropertyAll(
          RoundedRectangleBorder(borderRadius: controlRadius),
        ),
        minimumSize: const WidgetStatePropertyAll(Size(44, 44)),
      ),
    ),
    iconButtonTheme: IconButtonThemeData(
      style: ButtonStyle(
        foregroundColor: WidgetStatePropertyAll(ink),
        minimumSize: const WidgetStatePropertyAll(Size(44, 44)),
        shape: WidgetStatePropertyAll(ark ? shape : const CircleBorder()),
        overlayColor: WidgetStatePropertyAll(accent.withValues(alpha: .15)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: ark ? background : Colors.white,
      contentPadding: const EdgeInsets.symmetric(horizontal: 13, vertical: 13),
      border: fieldBorder,
      enabledBorder: fieldBorder,
      focusedBorder: fieldBorder.copyWith(
        borderSide: BorderSide(color: accent, width: width),
      ),
      errorBorder: fieldBorder.copyWith(
        borderSide: BorderSide(color: scheme.error, width: width),
      ),
      focusedErrorBorder: fieldBorder.copyWith(
        borderSide: BorderSide(color: scheme.error, width: width),
      ),
      labelStyle: typography.labelMedium,
      hintStyle: typography.bodyMedium?.copyWith(
        color: ark ? ForFilmColors.arkHint : const Color(0xffa9a296),
      ),
      helperStyle: typography.bodySmall,
      errorStyle: typography.bodySmall?.copyWith(color: scheme.error),
    ),
    dialogTheme: DialogThemeData(
      backgroundColor: surface,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      shape: RoundedRectangleBorder(borderRadius: cardRadius, side: side),
      titleTextStyle: typography.titleLarge,
      contentTextStyle: typography.bodyMedium,
    ),
    bottomSheetTheme: BottomSheetThemeData(
      backgroundColor: surface,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(ark ? 0 : 20)),
        side: side,
      ),
      showDragHandle: !ark,
    ),
    popupMenuTheme: PopupMenuThemeData(
      color: surface,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      shape: shape,
      textStyle: typography.bodyMedium,
    ),
    dropdownMenuTheme: DropdownMenuThemeData(
      textStyle: typography.bodyLarge,
      inputDecorationTheme: InputDecorationTheme(
        border: fieldBorder,
        enabledBorder: fieldBorder,
        filled: true,
        fillColor: ark ? background : Colors.white,
      ),
      menuStyle: MenuStyle(
        backgroundColor: WidgetStatePropertyAll(surface),
        surfaceTintColor: const WidgetStatePropertyAll(Colors.transparent),
        elevation: const WidgetStatePropertyAll(0),
        shape: WidgetStatePropertyAll(shape),
        side: WidgetStatePropertyAll(side),
      ),
    ),
    chipTheme: ChipThemeData(
      backgroundColor: ark ? background : Colors.white,
      selectedColor: ark ? ForFilmColors.arkAccent : ForFilmColors.lemon,
      disabledColor: line.withValues(alpha: .2),
      labelStyle: typography.labelMedium?.copyWith(color: ink),
      side: side,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(ark ? 0 : 12),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
      showCheckmark: true,
      checkmarkColor: onPrimary,
    ),
    switchTheme: SwitchThemeData(
      thumbColor: WidgetStatePropertyAll(ark ? ink : Colors.white),
      trackColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? (ark ? ForFilmColors.arkAccent : ForFilmColors.mint)
            : (ark ? line : ForFilmColors.creamSoft),
      ),
      trackOutlineColor: WidgetStatePropertyAll(line),
      trackOutlineWidth: WidgetStatePropertyAll(ark ? 0 : 3),
      overlayColor: WidgetStatePropertyAll(accent.withValues(alpha: .15)),
    ),
    checkboxTheme: CheckboxThemeData(
      side: side,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(ark ? 0 : 4),
      ),
      fillColor: WidgetStateProperty.resolveWith(
        (states) => states.contains(WidgetState.selected)
            ? (ark ? ForFilmColors.arkAccent : ForFilmColors.mint)
            : Colors.transparent,
      ),
      checkColor: WidgetStatePropertyAll(onPrimary),
    ),
    sliderTheme: SliderThemeData(
      trackHeight: ark ? 4 : 6,
      activeTrackColor: ark ? ForFilmColors.arkAccent : ForFilmColors.mint,
      inactiveTrackColor: ark ? line : ForFilmColors.creamSoft,
      thumbColor: ink,
      overlayColor: accent.withValues(alpha: .15),
      thumbShape: _ForFilmSliderThumb(ark: ark),
      valueIndicatorColor: ink,
      valueIndicatorTextStyle: typography.labelSmall?.copyWith(
        color: onPrimary,
      ),
    ),
    segmentedButtonTheme: SegmentedButtonThemeData(
      style: ButtonStyle(
        shape: WidgetStatePropertyAll(shape),
        side: WidgetStatePropertyAll(side),
        textStyle: WidgetStatePropertyAll(typography.labelLarge),
        foregroundColor: WidgetStateProperty.resolveWith(
          (states) => states.contains(WidgetState.selected) ? onPrimary : ink,
        ),
        backgroundColor: WidgetStateProperty.resolveWith(
          (states) => states.contains(WidgetState.selected)
              ? (ark ? ForFilmColors.arkAccent : ForFilmColors.lemon)
              : (ark ? background : Colors.white),
        ),
      ),
    ),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: background,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
      height: 72,
      indicatorColor: primary,
      indicatorShape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(ark ? 0 : 12),
      ),
      labelTextStyle: WidgetStateProperty.resolveWith(
        (states) => typography.labelSmall?.copyWith(
          color: states.contains(WidgetState.selected) ? ink : muted,
        ),
      ),
      iconTheme: WidgetStateProperty.resolveWith(
        (states) => IconThemeData(
          size: 20,
          color: states.contains(WidgetState.selected) ? onPrimary : muted,
        ),
      ),
    ),
    navigationRailTheme: NavigationRailThemeData(
      backgroundColor: Colors.transparent,
      elevation: 0,
      indicatorColor: primary,
      indicatorShape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(ark ? 0 : 12),
      ),
      selectedIconTheme: IconThemeData(color: onPrimary, size: 20),
      unselectedIconTheme: IconThemeData(color: muted, size: 20),
      selectedLabelTextStyle: typography.labelLarge?.copyWith(color: ink),
      unselectedLabelTextStyle: typography.labelLarge?.copyWith(color: muted),
    ),
    listTileTheme: ListTileThemeData(
      iconColor: ink,
      textColor: ink,
      titleTextStyle: typography.titleSmall,
      subtitleTextStyle: typography.bodySmall,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      shape: RoundedRectangleBorder(borderRadius: controlRadius),
    ),
    dividerTheme: DividerThemeData(color: line, thickness: width, space: 24),
    progressIndicatorTheme: ProgressIndicatorThemeData(
      color: ark ? ForFilmColors.arkAccent : ForFilmColors.mint,
      linearTrackColor: ark ? line : ForFilmColors.creamSoft,
      linearMinHeight: ark ? 4 : 6,
      borderRadius: BorderRadius.circular(ark ? 0 : 8),
    ),
    snackBarTheme: SnackBarThemeData(
      backgroundColor: ark ? ForFilmColors.arkRaised : Colors.white,
      contentTextStyle: typography.bodyMedium,
      actionTextColor: ark ? ForFilmColors.arkAccent : ForFilmColors.ink,
      elevation: 0,
      behavior: SnackBarBehavior.floating,
      shape: shape,
    ),
    tooltipTheme: TooltipThemeData(
      decoration: BoxDecoration(
        color: ark ? ForFilmColors.arkRaised : ForFilmColors.ink,
        borderRadius: controlRadius,
        border: Border.all(color: line, width: width),
      ),
      textStyle: typography.bodySmall?.copyWith(
        color: ark ? ink : Colors.white,
      ),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
    ),
    scrollbarTheme: ScrollbarThemeData(
      thumbColor: WidgetStatePropertyAll(ark ? line : ink),
      radius: Radius.circular(ark ? 0 : 8),
      thickness: const WidgetStatePropertyAll(6),
    ),
  );
}

class _ForFilmSliderThumb extends SliderComponentShape {
  const _ForFilmSliderThumb({required this.ark});
  final bool ark;
  @override
  Size getPreferredSize(bool isEnabled, bool isDiscrete) =>
      ark ? const Size(16, 22) : const Size(22, 22);
  @override
  void paint(
    PaintingContext context,
    Offset center, {
    required Animation<double> activationAnimation,
    required Animation<double> enableAnimation,
    required bool isDiscrete,
    required TextPainter labelPainter,
    required RenderBox parentBox,
    required SliderThemeData sliderTheme,
    required TextDirection textDirection,
    required double value,
    required double textScaleFactor,
    required Size sizeWithOverflow,
  }) {
    final canvas = context.canvas;
    final fill = Paint()
      ..color = ark ? ForFilmColors.arkInk : ForFilmColors.sun;
    final stroke = Paint()
      ..color = ark ? ForFilmColors.arkBackground : ForFilmColors.ink
      ..style = PaintingStyle.stroke
      ..strokeWidth = ark ? 1 : 3;
    if (ark) {
      final rect = Rect.fromCenter(center: center, width: 16, height: 22);
      canvas.drawRect(rect, fill);
      canvas.drawRect(rect, stroke);
    } else {
      canvas.drawCircle(center, 10, fill);
      canvas.drawCircle(center, 10, stroke);
    }
  }
}
