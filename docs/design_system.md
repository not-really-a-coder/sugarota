# Sugarota Companion: Color Palette & Design System Document

This document defines the formal color system, design tokens, semantic mappings, and visual hierarchy utilized across the **Sugarota Companion Android App** (`org.sugarota.companion`).

---

## 1. Design System Philosophy

The design system for Sugarota Companion is built on the **Shadcn Zinc (Dark Mode)** aesthetic, tailored specifically for health and continuous glucose monitoring (CGM).

### Core Principles:
1. **OLED True Dark (`#09090B`)**: Deep near-black canvas that minimizes battery draw on mobile devices and provides high contrast in low-light environments.
2. **Emerald Primary Accent (`#00E676`)**: High-visibility medical green symbolizing healthy telemetry, active BLE connection, and in-target state.
3. **Medical Severity Semantics**: Immediate color-coded feedback for blood glucose levels (Normal, Borderline, Severe/Urgent).
4. **Subtle Surface Layering**: Surfaces, cards, and interactive controls use tonal increments (`zinc-900` to `zinc-700`) with hairline borders (`1dp`) rather than heavy drop shadows.

---

## 2. Core Color Tokens

### 2.1 Theme Swatches (Zinc Dark)

| Token Name | Hex Code | RGB | Tailwind Reference | Semantic Role / Usage |
|---|---|---|---|---|
| `background` | `#09090B` | `rgb(9, 9, 11)` | `zinc-950` | Primary app canvas and screen background |
| `foreground` | `#FAFAFA` | `rgb(250, 250, 250)` | `zinc-50` | High-contrast primary text, headings, icons |
| `card` | `#18181B` | `rgb(24, 24, 27)` | `zinc-900` | Surface layer for device cards, dialogs, containers |
| `cardForeground` | `#FAFAFA` | `rgb(250, 250, 250)` | `zinc-50` | Text and elements on card surfaces |
| `popover` | `#18181B` | `rgb(24, 24, 27)` | `zinc-900` | Popovers, dropdown menus, modals |
| `popoverForeground` | `#FAFAFA` | `rgb(250, 250, 250)` | `zinc-50` | Popover text |
| `secondary` | `#27272A` | `rgb(39, 39, 42)` | `zinc-800` | Secondary buttons, chips, inactive tabs |
| `secondaryForeground` | `#FAFAFA` | `rgb(250, 250, 250)` | `zinc-50` | Secondary button labels and icons |
| `muted` | `#27272A` | `rgb(39, 39, 42)` | `zinc-800` | Muted background fills, dividers, inactive states |
| `mutedForeground` | `#A1A1AA` | `rgb(161, 161, 170)` | `zinc-400` | Subtitles, timestamps, captions, secondary labels |
| `border` | `#27272A` | `rgb(39, 39, 42)` | `zinc-800` | Default 1dp card borders, dividers, outlines |
| `inputBorder` | `#3F3F46` | `rgb(63, 63, 70)` | `zinc-700` | Input field borders (unfocused) |
| `ring` | `#00E676` | `rgb(0, 230, 118)` | Custom Emerald | Focus rings, highlight glows, active indicators |

---

## 3. Brand & Accent Palette

### 3.1 Primary Accent
* **Emerald Green (`#00E676`)**:
  - Main call-to-action buttons (`Connect`, `Sync Now`, `Save Configuration`)
  - Pull-to-refresh spinner and `"Scanning..."` text indicator
  - Device active badges and connected radio status
  - App brand highlights

### 3.2 Medical Glucose Telemetry Semantics

Glucose values (`SGV` in mg/dL) are mapped dynamically across four distinct physiological categories:

```text
  LOW / SEVERE LOW        BORDERLINE / NEAR TARGET        IN TARGET (NORMAL)        HIGH / SEVERE HIGH
[ 0 < SGV < 55 mg/dL ]     [ 55 <= SGV < 70 mg/dL ]      [ 70 <= SGV <= 180 mg/dL ]    [ SGV > 180 mg/dL ]
      #EF4444                     #F97316                       #00E676                    #F97316 / #EF4444
     (Red-500)                  (Orange-500)                  (Emerald-400)                  (Warning / Urgent)
```

| Range (mg/dL) | Classification | Color Token | Hex Code | Visual Styling |
|---|---|---|---|---|
| **$\le 0$** | Invalid / Stale / Null | `Gray` | `#848484` | Neutral muted reading |
| **$< 55$** | Urgent Low (Hypoglycemia) | `Red` (`destructive`) | `#EF4444` | High-alert red glow, badge & arrow |
| **$55 - 69$** | Low / Borderline | `Orange` | `#F97316` | Cautionary amber/orange |
| **$70 - 180$** | In-Target (Euglycemia) | `Emerald` (`primary`) | `#00E676` | Calm, reassuring bright green |
| **$181 - 229$** | High / Rising | `Orange` | `#F97316` | Cautionary amber/orange |
| **$\ge 230$** | Severe High (Hyperglycemia) | `Red` (`destructive`) | `#EF4444` | High-alert red |

---

## 4. Component Color Specs

### 4.1 Buttons (`ShadcnButton`)

| Variant | Background | Text / Icon Color | Border | Disabled State |
|---|---|---|---|---|
| `DEFAULT` | `#00E676` (`primary`) | `#000000` (`primaryForeground`) | None | 50% opacity (`#00E676` @ 0.5) |
| `SECONDARY` | `#27272A` (`secondary`) | `#FAFAFA` (`secondaryForeground`) | None | 50% opacity |
| `OUTLINE` | `Transparent` | `#FAFAFA` (`foreground`) | `1dp` `#27272A` (`border`) | 50% opacity |
| `GHOST` | `Transparent` | `#FAFAFA` (`foreground`) | None | 50% opacity |
| `DESTRUCTIVE`| `#EF4444` (`destructive`) | `#FAFAFA` (`destructiveForeground`) | None | 50% opacity |

### 4.2 Status Badges (`ShadcnBadge`)

| Variant | Background | Text Color | Border |
|---|---|---|---|
| `DEFAULT` | `rgba(0, 230, 118, 0.15)` | `#00E676` | `1dp` `rgba(0, 230, 118, 0.30)` |
| `SECONDARY` | `#27272A` (`secondary`) | `#A1A1AA` (`mutedForeground`) | `1dp` `#27272A` (`border`) |
| `DESTRUCTIVE`| `rgba(239, 68, 68, 0.15)` | `#EF4444` | `1dp` `rgba(239, 68, 68, 0.30)` |

### 4.3 Form Inputs (`ShadcnInput`)
* **Container Fill**: `#09090B` (`colors.background`)
* **Border (Default)**: `1dp` `#3F3F46` (`colors.inputBorder`)
* **Border (Focused)**: `1dp` `#00E676` (`colors.ring`)
* **Label**: `12sp Medium` `#A1A1AA` (`colors.mutedForeground`)
* **Text**: `14sp Normal` `#FAFAFA` (`colors.foreground`)

### 4.4 App Brand Icon & Illustration Palette
* **Droplet Base**: Coral Rose (`#F0526B` / `#FF3B69`)
* **Cube Facets**: 
  - Top: Pure Soft White (`#F5F8FF`)
  - Left: Frost Blue (`#DDEBFA`)
  - Right: Ice Blue (`#BED8F4`)
* **Circular Lens Highlight**: Semi-translucent Soft Frost (`rgba(255, 255, 255, 0.35)`)

---

## 5. Typography & Contrast Compliance

All foreground colors against `#09090B` and `#18181B` satisfy **WCAG 2.1 Level AAA** contrast requirements:

| Text Level | Color Token | Contrast Ratio on `#09090B` | Standard Compliance |
|---|---|---|---|
| **Primary Headings & Body** | `#FAFAFA` | **19.1:1** | WCAG AAA (Pass) |
| **Secondary & Captions** | `#A1A1AA` | **8.4:1** | WCAG AAA (Pass) |
| **Emerald Accent Text** | `#00E676` | **11.2:1** | WCAG AAA (Pass) |
| **Urgent Warning Text** | `#EF4444` | **5.3:1** | WCAG AA Large/Bold (Pass) |

---

## 6. Firmware RGB565 Color Mapping (`config.h`)

The ESP32-S3 firmware uses direct 16-bit 5-6-5 RGB color codes matching the design tokens:

| Token Name | Hex Code (24-bit) | RGB565 Literal | Usage in Firmware |
|---|---|---|---|
| `BLACK` | `#09090B` | `0x0841` | OLED Zinc dark screen canvas background |
| `WHITE` | `#FAFAFA` | `0xF7BE` | Primary high-contrast text & digits in dark theme |
| `GREEN` | `#00E676` | `0x072E` | Normal glucose values, in-target chart points, active timer |
| `ORANGE`| `#F97316` | `0xFBA2` | Borderline / warning glucose values, status spinners |
| `RED`   | `#EF4444` | `0xEF48` | Urgent high/low glucose, critical battery, offline notice |
| `GRAY`  | `#848484` | `0x8410` | Inactive reading, dialog card background |
| `ZINC_BORDER` | `#27272A` | `0x2104` | Hairline status bar separator & container borders |
| `CYAN`  | `#00F0FF` | `0x07FF` | Connected BLE icon, reading age timestamp |
| `DARK_RED` | `#880000` | `0x8800` | Data gap message text, high-contrast dialog NO text |
| `LIGHT_PINK` | `#FCE7F3` | `0xFDF3` | Dialog NO button background |

---

## 7. Implementation References
* Kotlin Theme Definition: [`ShadcnTheme.kt`](../android-app/app/src/main/java/org/sugarota/companion/ui/theme/ShadcnTheme.kt)
* Reusable UI Components: [`ShadcnComponents.kt`](../android-app/app/src/main/java/org/sugarota/companion/ui/components/ShadcnComponents.kt)
* Dynamic Glucose Color Mapping: [`MainActivity.kt`](../android-app/app/src/main/java/org/sugarota/companion/MainActivity.kt) (`getGlucoseColor()`)
* Firmware Color Definitions: [`config.h`](../firmware/sugarota/config.h)
* Firmware UI Renderer: [`ui.cpp`](../firmware/sugarota/ui.cpp)
