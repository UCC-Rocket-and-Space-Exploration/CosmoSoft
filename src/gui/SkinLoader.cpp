#include "gui/SkinLoader.h"

#include "SkinArchive.h"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

using namespace cosmo;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

namespace {

constexpr qsizetype kMaxThemeBytes = 256 * 1024;
constexpr qsizetype kMaxImageBytes = 8 * 1024 * 1024;
constexpr int kMaxImageDimension = 4096;
constexpr quint64 kMaxDecodedImagePixels = 20ULL * 1024ULL * 1024ULL;
constexpr double kMaxTextureOpacity = 0.20;
constexpr double kMinimumTraceColorDeltaE = 10.0;
constexpr std::size_t kTraceColorCount = 9;

QMutex &skinImportMutex() {
    static QMutex mutex;
    return mutex;
}

QRecursiveMutex &skinCatalogMutex() {
    static QRecursiveMutex mutex;
    return mutex;
}

void assignError(QString *error_message, const QString &message) {
    if (error_message) {
        *error_message = message;
    }
}

bool readOptionalString(
    const QJsonObject &object,
    const QString &key,
    QString &value,
    int maximum_length,
    QString *error_message) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object.value(key).isString()) {
        assignError(error_message, QStringLiteral("theme.json field '%1' must be a string.").arg(key));
        return false;
    }
    const QString candidate = object.value(key).toString().trimmed();
    if (candidate.isEmpty() || candidate.size() > maximum_length) {
        assignError(
            error_message,
            QStringLiteral("theme.json field '%1' must contain 1 to %2 characters.")
                .arg(key)
                .arg(maximum_length));
        return false;
    }
    value = candidate;
    return true;
}

QString canonicalColor(const QColor &color) {
    return color.alpha() == 255
        ? color.name(QColor::HexRgb)
        : color.name(QColor::HexArgb);
}

double linearChannel(double channel) {
    channel /= 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

const std::array<double, 256> &linearChannelLookup() {
    static const std::array<double, 256> lookup = [] {
        std::array<double, 256> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = linearChannel(static_cast<double>(index));
        }
        return values;
    }();
    return lookup;
}

double relativeLuminance(const QColor &color) {
    const auto &lookup = linearChannelLookup();
    return 0.2126 * lookup[static_cast<std::size_t>(color.red())]
        + 0.7152 * lookup[static_cast<std::size_t>(color.green())]
        + 0.0722 * lookup[static_cast<std::size_t>(color.blue())];
}

double contrastRatioFromLuminance(double foreground, double background) {
    const double lighter = std::max(foreground, background);
    const double darker = std::min(foreground, background);
    return (lighter + 0.05) / (darker + 0.05);
}

bool hasMinimumContrast(double foreground, double background, double minimum) {
    const double lighter = std::max(foreground, background);
    const double darker = std::min(foreground, background);
    return lighter + 0.05 >= (minimum - 0.001) * (darker + 0.05);
}

double contrastRatio(const QColor &foreground, const QColor &background) {
    return contrastRatioFromLuminance(
        relativeLuminance(foreground), relativeLuminance(background));
}

double contrastRatio(const QString &foreground, const QString &background) {
    return contrastRatio(QColor(foreground), QColor(background));
}

struct LabColor {
    double lightness = 0.0;
    double a = 0.0;
    double b = 0.0;
};

double labPivot(double component) {
    constexpr double kEpsilon = 216.0 / 24389.0;
    constexpr double kKappa = 24389.0 / 27.0;
    return component > kEpsilon
        ? std::cbrt(component)
        : (kKappa * component + 16.0) / 116.0;
}

LabColor toLab(const QColor &color) {
    const double red = linearChannel(color.red());
    const double green = linearChannel(color.green());
    const double blue = linearChannel(color.blue());

    // D65 reference white and the standard sRGB-to-XYZ matrix.
    const double x = (0.4124564 * red + 0.3575761 * green + 0.1804375 * blue)
        / 0.95047;
    const double y = 0.2126729 * red + 0.7151522 * green + 0.0721750 * blue;
    const double z = (0.0193339 * red + 0.1191920 * green + 0.9503041 * blue)
        / 1.08883;
    const double fx = labPivot(x);
    const double fy = labPivot(y);
    const double fz = labPivot(z);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

double colorDistance(const QColor &first, const QColor &second) {
    const LabColor first_lab = toLab(first);
    const LabColor second_lab = toLab(second);
    const double lightness_delta = first_lab.lightness - second_lab.lightness;
    const double a_delta = first_lab.a - second_lab.a;
    const double b_delta = first_lab.b - second_lab.b;
    return std::sqrt(lightness_delta * lightness_delta
                     + a_delta * a_delta
                     + b_delta * b_delta);
}

std::array<QString, kTraceColorCount> fallbackTraceColors(
    const QString &base_background,
    const QString &panel_background) {
    static const std::array<QColor, kTraceColorCount> bright_colors = {
        QColor(QStringLiteral("#5b9bd5")),
        QColor(QStringLiteral("#70c1a5")),
        QColor(QStringLiteral("#f0b429")),
        QColor(QStringLiteral("#c084fc")),
        QColor(QStringLiteral("#ff6b6b")),
        QColor(QStringLiteral("#ff9f6b")),
        QColor(QStringLiteral("#f472b6")),
        QColor(QStringLiteral("#a3e635")),
        QColor(QStringLiteral("#f8de22")),
    };
    static const std::array<QColor, kTraceColorCount> dark_colors = {
        QColor(QStringLiteral("#1d5d90")),
        QColor(QStringLiteral("#16705a")),
        QColor(QStringLiteral("#8a5a00")),
        QColor(QStringLiteral("#6d28d9")),
        QColor(QStringLiteral("#b42318")),
        QColor(QStringLiteral("#9a3e00")),
        QColor(QStringLiteral("#a61e63")),
        QColor(QStringLiteral("#3f6212")),
        QColor(QStringLiteral("#665c00")),
    };

    std::array<QString, kTraceColorCount> colors;
    const auto minimum_contrast = [&](const QColor &color) {
        const QString canonical = canonicalColor(color);
        return std::min(contrastRatio(canonical, base_background),
                        contrastRatio(canonical, panel_background));
    };
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const QColor bright = bright_colors[index];
        const QColor dark = dark_colors[index];
        const double bright_contrast = minimum_contrast(bright);
        const double dark_contrast = minimum_contrast(dark);
        QColor candidate = bright_contrast >= dark_contrast ? bright : dark;

        if (std::max(bright_contrast, dark_contrast) < 3.25) {
            const QColor black(Qt::black);
            const QColor white(Qt::white);
            const QColor target = minimum_contrast(black) >= minimum_contrast(white)
                ? black
                : white;
            for (int step = 1; step <= 20; ++step) {
                const double amount = static_cast<double>(step) / 20.0;
                const QColor adjusted = QColor::fromRgbF(
                    static_cast<float>(candidate.redF() * (1.0 - amount)
                                       + target.redF() * amount),
                    static_cast<float>(candidate.greenF() * (1.0 - amount)
                                       + target.greenF() * amount),
                    static_cast<float>(candidate.blueF() * (1.0 - amount)
                                       + target.blueF() * amount));
                candidate = adjusted;
                if (minimum_contrast(candidate) >= 3.25) {
                    break;
                }
            }
        }
        colors[index] = canonicalColor(candidate);
    }
    return colors;
}

bool requireContrast(
    const QString &foreground_key,
    const QString &foreground,
    const QString &background_key,
    const QString &background,
    double minimum,
    QString *error_message) {
    const double ratio = contrastRatio(foreground, background);
    if (ratio + 0.001 >= minimum) {
        return true;
    }
    assignError(
        error_message,
        QStringLiteral("Palette colors '%1' and '%2' have %3:1 contrast; at least %4:1 is required.")
            .arg(foreground_key, background_key)
            .arg(ratio, 0, 'f', 2)
            .arg(minimum, 0, 'f', 1));
    return false;
}

bool validatePaletteContrast(
    const ColorPalette &palette,
    bool validate_trace_distances,
    QString *error_message) {
    const std::array<std::pair<QString, QString>, 7> primary_backgrounds{{
        {QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("bg_panel"), palette.bg_panel},
        {QStringLiteral("bg_input"), palette.bg_input},
        {QStringLiteral("bg_button"), palette.bg_button},
        {QStringLiteral("btn_hover"), palette.btn_hover},
        {QStringLiteral("btn_pressed"), palette.btn_pressed},
    }};
    for (const auto &[key, color] : primary_backgrounds) {
        if (!requireContrast(
                QStringLiteral("text_primary"), palette.text_primary,
                key, color, 4.5, error_message)) {
            return false;
        }
    }

    const std::array<std::pair<QString, QString>, 3> secondary_backgrounds{{
        {QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("bg_panel"), palette.bg_panel},
    }};
    const std::array<std::pair<QString, QString>, 3> secondary_text{{
        {QStringLiteral("text_mid"), palette.text_mid},
        {QStringLiteral("text_muted"), palette.text_muted},
        {QStringLiteral("text_dim"), palette.text_dim},
    }};
    for (const auto &[text_key, text_color] : secondary_text) {
        for (const auto &[background_key, background] : secondary_backgrounds) {
            if (!requireContrast(text_key, text_color, background_key, background,
                                 4.5, error_message)) {
                return false;
            }
        }
    }

    // QPalette::PlaceholderText is drawn on Base, while disabled text is
    // drawn on input, button, panel, and window surfaces depending on widget.
    const std::array<std::tuple<QString, QString, QString, QString>, 3>
        input_and_button_text_pairs{{
            {QStringLiteral("text_dim"), palette.text_dim,
             QStringLiteral("bg_input"), palette.bg_input},
            {QStringLiteral("text_muted"), palette.text_muted,
             QStringLiteral("bg_input"), palette.bg_input},
            {QStringLiteral("text_muted"), palette.text_muted,
             QStringLiteral("bg_button"), palette.bg_button},
        }};
    for (const auto &[text_key, text_color, background_key, background]
         : input_and_button_text_pairs) {
        if (!requireContrast(text_key, text_color, background_key, background,
                             4.5, error_message)) {
            return false;
        }
    }

    // Links can be provided by QPalette on Window, Base, AlternateBase, and
    // custom dark panels, so every surface on which link text is rendered is
    // validated rather than only the top-level window background.
    const std::array<std::pair<QString, QString>, 4> link_backgrounds{{
        {QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("bg_panel"), palette.bg_panel},
        {QStringLiteral("bg_input"), palette.bg_input},
    }};
    for (const auto &[background_key, background] : link_backgrounds) {
        if (!requireContrast(
                QStringLiteral("accent_link"), palette.accent_link,
                background_key, background, 4.5, error_message)) {
            return false;
        }
    }

    const std::array<std::tuple<QString, QString, QString, QString, double>, 18> ui_pairs{{
        {QStringLiteral("text_primary"), palette.text_primary,
         QStringLiteral("select_bg"), palette.select_bg, 4.5},
        {QStringLiteral("danger"), palette.danger,
         QStringLiteral("bg_base"), palette.bg_base, 4.5},
        {QStringLiteral("error"), palette.error,
         QStringLiteral("bg_dark"), palette.bg_dark, 4.5},
        {QStringLiteral("success"), palette.success,
         QStringLiteral("success_bg"), palette.success_bg, 4.5},
        {QStringLiteral("warning"), palette.warning,
         QStringLiteral("warning_bg"), palette.warning_bg, 4.5},
        {QStringLiteral("info"), palette.info,
         QStringLiteral("info_bg"), palette.info_bg, 4.5},
        {QStringLiteral("focus_ring"), palette.focus_ring,
         QStringLiteral("bg_base"), palette.bg_base, 3.0},
        {QStringLiteral("focus_ring"), palette.focus_ring,
         QStringLiteral("bg_input"), palette.bg_input, 3.0},
        {QStringLiteral("focus_ring"), palette.focus_ring,
         QStringLiteral("bg_button"), palette.bg_button, 3.0},
        {QStringLiteral("focus_ring"), palette.focus_ring,
         QStringLiteral("bg_panel"), palette.bg_panel, 3.0},
        {QStringLiteral("accent_checkbox"), palette.accent_checkbox,
         QStringLiteral("bg_input"), palette.bg_input, 3.0},
        {QStringLiteral("border_default"), palette.border_default,
         QStringLiteral("bg_base"), palette.bg_base, 3.0},
        {QStringLiteral("border_default"), palette.border_default,
         QStringLiteral("bg_input"), palette.bg_input, 3.0},
        {QStringLiteral("border_default"), palette.border_default,
         QStringLiteral("bg_panel"), palette.bg_panel, 3.0},
        {QStringLiteral("border_light"), palette.border_light,
         QStringLiteral("bg_button"), palette.bg_button, 3.0},
        {QStringLiteral("border_panel"), palette.border_panel,
         QStringLiteral("bg_panel"), palette.bg_panel, 3.0},
        {QStringLiteral("select_bg"), palette.select_bg,
         QStringLiteral("bg_base"), palette.bg_base, 3.0},
        {QStringLiteral("accent_checkbox_border"), palette.accent_checkbox_border,
         QStringLiteral("bg_input"), palette.bg_input, 3.0},
    }};
    for (const auto &[foreground_key, foreground, background_key, background, minimum] : ui_pairs) {
        if (!requireContrast(foreground_key, foreground, background_key, background,
                             minimum, error_message)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < palette.trace_colors.size(); ++index) {
        const QString key = QStringLiteral("trace_colors[%1]").arg(index);
        if (!requireContrast(key, palette.trace_colors[index],
                             QStringLiteral("bg_base"), palette.bg_base,
                             3.0, error_message)
            || !requireContrast(key, palette.trace_colors[index],
                                QStringLiteral("bg_panel"), palette.bg_panel,
                                3.0, error_message)) {
            return false;
        }
    }

    if (validate_trace_distances) {
        // A 10-point CIE76 distance rejects both canonical duplicates and
        // user-selected colors that are too close to identify as separate traces.
        for (std::size_t first = 0; first < palette.trace_colors.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < palette.trace_colors.size(); ++second) {
                const double distance = colorDistance(
                    QColor(palette.trace_colors[first]),
                    QColor(palette.trace_colors[second]));
                if (distance + 0.001 < kMinimumTraceColorDeltaE) {
                    assignError(
                        error_message,
                        QStringLiteral(
                            "Palette colors 'trace_colors[%1]' and 'trace_colors[%2]' "
                            "are indistinguishable (%3 CIE76 Delta-E); at least %4 is required.")
                            .arg(first)
                            .arg(second)
                            .arg(distance, 0, 'f', 2)
                            .arg(kMinimumTraceColorDeltaE, 0, 'f', 1));
                    return false;
                }
            }
        }
    }
    return true;
}

bool validatePng(
    const QByteArray &bytes,
    const QString &name,
    quint64 &decoded_pixels,
    QString &error_message,
    QImage *decoded_image = nullptr) {
    static const QByteArray png_signature = QByteArray::fromHex("89504e470d0a1a0a");
    if (bytes.size() > kMaxImageBytes || !bytes.startsWith(png_signature)) {
        error_message = QStringLiteral("%1 is not a valid bounded PNG image.").arg(name);
        return false;
    }

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        error_message = QStringLiteral("Could not inspect image %1.").arg(name);
        return false;
    }
    QImageReader reader(&buffer, "PNG");
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid()
        || size.width() > kMaxImageDimension || size.height() > kMaxImageDimension) {
        error_message = QStringLiteral("%1 must be a valid PNG no larger than 4096x4096 pixels.").arg(name);
        return false;
    }
    const QImage decoded = reader.read();
    if (decoded.isNull() || decoded.width() > kMaxImageDimension
        || decoded.height() > kMaxImageDimension) {
        error_message = QStringLiteral("Could not safely decode PNG image %1.").arg(name);
        return false;
    }
    decoded_pixels = static_cast<quint64>(decoded.width())
        * static_cast<quint64>(decoded.height());
    if (decoded_image) {
        *decoded_image = decoded;
    }
    return true;
}

bool readValidatedPng(
    const QString &path,
    const QString &display_name,
    quint64 &decoded_pixels,
    QImage *decoded_image,
    QString &error_message) {
    const QFileInfo image_info(path);
    if (!image_info.exists() || !image_info.isFile() || image_info.isSymLink()) {
        error_message = QStringLiteral("%1 must be a regular PNG file.").arg(display_name);
        return false;
    }

    QFile image(path);
    if (!image.open(QIODevice::ReadOnly)) {
        error_message = QStringLiteral("Could not open image %1: %2")
                            .arg(display_name, image.errorString());
        return false;
    }
    const qint64 image_size = image.size();
    if (image_size <= 0 || image_size > kMaxImageBytes) {
        error_message = QStringLiteral("%1 is not a valid bounded PNG image.").arg(display_name);
        return false;
    }
    const QByteArray bytes = image.read(kMaxImageBytes + 1);
    if (bytes.size() != image_size) {
        error_message = QStringLiteral("Could not read the complete image %1.").arg(display_name);
        return false;
    }

    if (!validatePng(
            bytes, display_name, decoded_pixels, error_message, decoded_image)) {
        return false;
    }
    return true;
}

bool validateInstalledPng(
    const QString &path,
    const QString &display_name,
    quint64 &decoded_pixel_total,
    QString &error_message) {
    quint64 image_pixels = 0;
    if (!readValidatedPng(
            path, display_name, image_pixels, nullptr, error_message)) {
        return false;
    }
    if (image_pixels > kMaxDecodedImagePixels - decoded_pixel_total) {
        error_message = QStringLiteral(
            "Skin images exceed the 20-megapixel decoded-image budget.");
        return false;
    }
    decoded_pixel_total += image_pixels;
    return true;
}

bool validateInstalledThemeImages(
    const CosmoTheme &theme,
    const QString &skin_dir,
    quint64 validated_panel_pixels,
    QString &error_message) {
    quint64 decoded_pixel_total = 0;
    if (!theme.preview_path.isEmpty()
        && !validateInstalledPng(
            theme.preview_path,
            QStringLiteral("preview.png"),
            decoded_pixel_total,
            error_message)) {
        return false;
    }

    const QDir skin_directory(skin_dir);
    for (auto it = theme.textures.paths.cbegin(); it != theme.textures.paths.cend(); ++it) {
        if (it.key() == QStringLiteral("panel") && validated_panel_pixels > 0) {
            if (validated_panel_pixels
                > kMaxDecodedImagePixels - decoded_pixel_total) {
                error_message = QStringLiteral(
                    "Skin images exceed the 20-megapixel decoded-image budget.");
                return false;
            }
            decoded_pixel_total += validated_panel_pixels;
            continue;
        }
        const QString display_name = skin_directory.relativeFilePath(it.value());
        if (!validateInstalledPng(
                it.value(), display_name, decoded_pixel_total, error_message)) {
            return false;
        }
    }
    return true;
}

bool validatePanelTextureContrast(
    const CosmoTheme &theme,
    const QString &skin_dir,
    QString *error_message,
    quint64 *validated_pixels) {
    if (validated_pixels) {
        *validated_pixels = 0;
    }
    const auto texture = theme.textures.paths.constFind(QStringLiteral("panel"));
    if (texture == theme.textures.paths.cend()) {
        return true;
    }

    const QString display_name = QDir(skin_dir).relativeFilePath(texture.value());
    quint64 decoded_pixels = 0;
    QImage decoded;
    QString image_error;
    if (!readValidatedPng(
            texture.value(), display_name, decoded_pixels, &decoded, image_error)) {
        assignError(error_message, image_error);
        return false;
    }
    if (validated_pixels) {
        *validated_pixels = decoded_pixels;
    }
    if (theme.textures.opacity <= 0.0) {
        return true;
    }

    const QImage pixels = decoded.format() == QImage::Format_ARGB32
        ? decoded
        : decoded.convertToFormat(QImage::Format_ARGB32);
    const QColor panel_background(theme.palette.bg_panel);
    const std::array<std::tuple<QString, double, double>, 6> contrast_targets{{
        {QStringLiteral("text_primary"),
         relativeLuminance(QColor(theme.palette.text_primary)), 4.5},
        {QStringLiteral("text_muted"),
         relativeLuminance(QColor(theme.palette.text_muted)), 4.5},
        {QStringLiteral("text_dim"),
         relativeLuminance(QColor(theme.palette.text_dim)), 4.5},
        {QStringLiteral("border_panel"),
         relativeLuminance(QColor(theme.palette.border_panel)), 3.0},
        {QStringLiteral("border_default"),
         relativeLuminance(QColor(theme.palette.border_default)), 3.0},
        {QStringLiteral("accent_link"),
         relativeLuminance(QColor(theme.palette.accent_link)), 3.0},
    }};
    const auto &linear_channels = linearChannelLookup();
    const double texture_opacity = theme.textures.opacity;
    const int panel_red = panel_background.red();
    const int panel_green = panel_background.green();
    const int panel_blue = panel_background.blue();

    for (int y = 0; y < pixels.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const QRgb source = row[x];
            const double alpha = texture_opacity
                * static_cast<double>(qAlpha(source)) / 255.0;
            const auto composite_channel = [alpha](
                                               int source_channel,
                                               int background_channel) {
                return static_cast<std::size_t>(std::clamp(
                    qRound(static_cast<double>(source_channel) * alpha
                           + static_cast<double>(background_channel) * (1.0 - alpha)),
                    0,
                    255));
            };
            const double rendered_background_luminance =
                0.2126 * linear_channels[composite_channel(qRed(source), panel_red)]
                + 0.7152 * linear_channels[composite_channel(qGreen(source), panel_green)]
                + 0.0722 * linear_channels[composite_channel(qBlue(source), panel_blue)];
            for (const auto &[foreground_key, foreground_luminance, minimum]
                 : contrast_targets) {
                if (!hasMinimumContrast(
                        foreground_luminance, rendered_background_luminance,
                        minimum)) {
                    const double ratio = contrastRatioFromLuminance(
                        foreground_luminance, rendered_background_luminance);
                    assignError(
                        error_message,
                        QStringLiteral(
                            "Panel texture '%1' makes '%2' contrast %3:1 at pixel "
                            "(%4,%5); at least %6:1 is required.")
                            .arg(display_name, foreground_key)
                            .arg(ratio, 0, 'f', 2)
                            .arg(x)
                            .arg(y)
                            .arg(minimum, 0, 'f', 1));
                    return false;
                }
            }
        }
    }
    return true;
}

bool writeStagedFile(
    const QString &root,
    const QString &relative_path,
    const QByteArray &contents,
    QString &error_message) {
    const QString output_path = QDir(root).filePath(relative_path);
    if (!QDir().mkpath(QFileInfo(output_path).path())) {
        error_message = QStringLiteral("Could not create the staging directory for %1.").arg(relative_path);
        return false;
    }
    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size()
        || !output.commit()) {
        error_message = QStringLiteral("Could not stage %1: %2").arg(relative_path, output.errorString());
        return false;
    }
    return true;
}

bool isSafeTexturePath(const QString &relative_path) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^textures/[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.png$)"));
    return pattern.match(relative_path).hasMatch();
}

bool isPathInside(const QString &base_path, const QString &candidate_path) {
    const QString canonical_base = QFileInfo(base_path).canonicalFilePath();
    const QString canonical_candidate = QFileInfo(candidate_path).canonicalFilePath();
    if (canonical_base.isEmpty() || canonical_candidate.isEmpty()) {
        return false;
    }
    const QString relative = QDir(canonical_base).relativeFilePath(canonical_candidate);
    return !QDir::isAbsolutePath(relative)
        && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"));
}

} // namespace

// ─── parseThemeJson ───────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::parseThemeJson(const QByteArray &json,
                                                     const QString &base_path,
                                                     QString *error_message,
                                                     bool enforce_contrast,
                                                     quint64 *validated_panel_pixels)
{
    if (validated_panel_pixels) {
        *validated_panel_pixels = 0;
    }
    if (json.isEmpty() || json.size() > kMaxThemeBytes) {
        assignError(error_message, QStringLiteral("theme.json must contain 1 byte to 256 KiB."));
        return std::nullopt;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) {
        assignError(
            error_message,
            QStringLiteral("Invalid theme.json at byte %1: %2")
                .arg(err.offset)
                .arg(err.errorString()));
        return std::nullopt;
    }

    const auto root = doc.object();

    CosmoTheme theme;
    theme.name = QStringLiteral("Custom");
    theme.author = QStringLiteral("Unknown");
    theme.version = QStringLiteral("1.0");
    if (!readOptionalString(root, QStringLiteral("name"), theme.name, 80, error_message)
        || !readOptionalString(root, QStringLiteral("author"), theme.author, 120, error_message)
        || !readOptionalString(root, QStringLiteral("version"), theme.version, 32, error_message)) {
        return std::nullopt;
    }

    const auto preview_file = base_path + QStringLiteral("/preview.png");
    if (QFile::exists(preview_file)) {
        const QFileInfo preview_info(preview_file);
        if (!base_path.startsWith(QStringLiteral(":/"))
            && (!preview_info.isFile() || preview_info.isSymLink()
                || !isPathInside(base_path, preview_file))) {
            assignError(error_message, QStringLiteral("preview.png must be a regular file inside the skin."));
            return std::nullopt;
        }
        theme.preview_path = preview_file;
    }

    // Palette
    bool trace_colors_supplied = false;
    if (root.contains(QStringLiteral("palette"))
        && !root.value(QStringLiteral("palette")).isObject()) {
        assignError(error_message, QStringLiteral("theme.json field 'palette' must be an object."));
        return std::nullopt;
    }
    if (root.contains(QStringLiteral("palette"))) {
        const auto pal = root[QStringLiteral("palette")].toObject();
        ColorPalette &p = theme.palette;

        struct PaletteField {
            const char *key;
            QString ColorPalette::*value;
        };
        static constexpr std::array fields{
            PaletteField{"bg_base", &ColorPalette::bg_base},
            PaletteField{"bg_dark", &ColorPalette::bg_dark},
            PaletteField{"bg_panel", &ColorPalette::bg_panel},
            PaletteField{"bg_input", &ColorPalette::bg_input},
            PaletteField{"bg_button", &ColorPalette::bg_button},
            PaletteField{"text_primary", &ColorPalette::text_primary},
            PaletteField{"text_mid", &ColorPalette::text_mid},
            PaletteField{"text_muted", &ColorPalette::text_muted},
            PaletteField{"text_dim", &ColorPalette::text_dim},
            PaletteField{"border_default", &ColorPalette::border_default},
            PaletteField{"border_light", &ColorPalette::border_light},
            PaletteField{"border_subtle", &ColorPalette::border_subtle},
            PaletteField{"border_panel", &ColorPalette::border_panel},
            PaletteField{"btn_hover", &ColorPalette::btn_hover},
            PaletteField{"btn_pressed", &ColorPalette::btn_pressed},
            PaletteField{"accent_link", &ColorPalette::accent_link},
            PaletteField{"accent_checkbox", &ColorPalette::accent_checkbox},
            PaletteField{"accent_checkbox_border", &ColorPalette::accent_checkbox_border},
            PaletteField{"danger", &ColorPalette::danger},
            PaletteField{"error", &ColorPalette::error},
            PaletteField{"select_bg", &ColorPalette::select_bg},
            PaletteField{"success", &ColorPalette::success},
            PaletteField{"success_bg", &ColorPalette::success_bg},
            PaletteField{"warning", &ColorPalette::warning},
            PaletteField{"warning_bg", &ColorPalette::warning_bg},
            PaletteField{"info", &ColorPalette::info},
            PaletteField{"info_bg", &ColorPalette::info_bg},
            PaletteField{"focus_ring", &ColorPalette::focus_ring},
            PaletteField{"focus_ring_offset", &ColorPalette::focus_ring_offset},
        };
        for (const auto &field : fields) {
            const QString key = QString::fromLatin1(field.key);
            if (!pal.contains(key)) {
                continue;
            }
            if (!pal.value(key).isString()) {
                assignError(
                    error_message,
                    QStringLiteral("Palette field '%1' must be a color string.").arg(key));
                return std::nullopt;
            }
            const QColor color(pal.value(key).toString().trimmed());
            if (!color.isValid()) {
                assignError(
                    error_message,
                    QStringLiteral("Palette field '%1' is not a valid QColor value.").arg(key));
                return std::nullopt;
            }
            if (key != QStringLiteral("focus_ring_offset") && color.alpha() != 255) {
                assignError(
                    error_message,
                    QStringLiteral("Palette color '%1' must be opaque for reliable contrast.").arg(key));
                return std::nullopt;
            }
            p.*(field.value) = canonicalColor(color);
        }

        if (pal.contains(QStringLiteral("trace_colors"))) {
            trace_colors_supplied = true;
            if (!pal.value(QStringLiteral("trace_colors")).isArray()) {
                assignError(error_message,
                            QStringLiteral("Palette field 'trace_colors' must be an array of 9 colors."));
                return std::nullopt;
            }
            const QJsonArray trace_colors = pal.value(QStringLiteral("trace_colors")).toArray();
            if (trace_colors.size() != static_cast<qsizetype>(kTraceColorCount)) {
                assignError(error_message,
                            QStringLiteral("Palette field 'trace_colors' must contain exactly 9 colors."));
                return std::nullopt;
            }
            for (qsizetype index = 0; index < trace_colors.size(); ++index) {
                if (!trace_colors[index].isString()) {
                    assignError(
                        error_message,
                        QStringLiteral("Palette field 'trace_colors[%1]' must be a color string.")
                            .arg(index));
                    return std::nullopt;
                }
                const QColor color(trace_colors[index].toString().trimmed());
                if (!color.isValid()) {
                    assignError(
                        error_message,
                        QStringLiteral("Palette field 'trace_colors[%1]' is not a valid QColor value.")
                            .arg(index));
                    return std::nullopt;
                }
                if (color.alpha() != 255) {
                    assignError(
                        error_message,
                        QStringLiteral("Palette color 'trace_colors[%1]' must be opaque for reliable contrast.")
                            .arg(index));
                    return std::nullopt;
                }
                p.trace_colors[static_cast<std::size_t>(index)] = canonicalColor(color);
            }
        } else {
            p.trace_colors = fallbackTraceColors(p.bg_base, p.bg_panel);
        }
    }

    const std::array<std::pair<QString, QString>, 11> backgrounds{{
        {QStringLiteral("bg_base"), theme.palette.bg_base},
        {QStringLiteral("bg_dark"), theme.palette.bg_dark},
        {QStringLiteral("bg_panel"), theme.palette.bg_panel},
        {QStringLiteral("bg_input"), theme.palette.bg_input},
        {QStringLiteral("bg_button"), theme.palette.bg_button},
        {QStringLiteral("btn_hover"), theme.palette.btn_hover},
        {QStringLiteral("btn_pressed"), theme.palette.btn_pressed},
        {QStringLiteral("select_bg"), theme.palette.select_bg},
        {QStringLiteral("success_bg"), theme.palette.success_bg},
        {QStringLiteral("warning_bg"), theme.palette.warning_bg},
        {QStringLiteral("info_bg"), theme.palette.info_bg},
    }};
    for (const auto &[key, value] : backgrounds) {
        if (QColor(value).alpha() != 255) {
            assignError(error_message, QStringLiteral("Palette background '%1' must be opaque.").arg(key));
            return std::nullopt;
        }
    }
    if (enforce_contrast
        && !validatePaletteContrast(
            theme.palette, trace_colors_supplied, error_message)) {
        return std::nullopt;
    }

    // Textures
    if (root.contains(QStringLiteral("textures"))
        && !root.value(QStringLiteral("textures")).isObject()) {
        assignError(error_message, QStringLiteral("theme.json field 'textures' must be an object."));
        return std::nullopt;
    }
    if (root.contains(QStringLiteral("textures"))) {
        const auto tex = root[QStringLiteral("textures")].toObject();
        static const QSet<QString> allowed_regions = {
            QStringLiteral("sidebar"),
            QStringLiteral("toolbar"),
            QStringLiteral("panel"),
            QStringLiteral("chart_bg"),
            QStringLiteral("settings_bg"),
        };
        QSet<QString> referenced_paths;
        for (auto it = tex.begin(); it != tex.end(); ++it) {
            if (!allowed_regions.contains(it.key())) {
                assignError(
                    error_message,
                    QStringLiteral("Texture region '%1' is not supported.").arg(it.key()));
                return std::nullopt;
            }
            if (!it.value().isString()) {
                assignError(
                    error_message,
                    QStringLiteral("Texture field '%1' must be a relative PNG path.").arg(it.key()));
                return std::nullopt;
            }
            const auto rel = it.value().toString();
            if (!isSafeTexturePath(rel)) {
                assignError(
                    error_message,
                    QStringLiteral("Texture field '%1' has an unsafe or unsupported path: %2")
                        .arg(it.key(), rel));
                return std::nullopt;
            }
            if (referenced_paths.contains(rel)) {
                assignError(
                    error_message,
                    QStringLiteral("Texture path '%1' must not be assigned to multiple regions.")
                        .arg(rel));
                return std::nullopt;
            }
            const auto full = QDir(base_path).filePath(rel);
            const QFileInfo texture_info(full);
            if (!texture_info.exists() || !texture_info.isFile() || texture_info.isSymLink()
                || !isPathInside(base_path, full)) {
                assignError(
                    error_message,
                    QStringLiteral("Referenced texture does not exist inside the skin: %1").arg(rel));
                return std::nullopt;
            }
            referenced_paths.insert(rel);
            theme.textures.paths.insert(it.key(), full);
        }
    }

    if (root.contains(QStringLiteral("texture_opacity"))) {
        if (!root.value(QStringLiteral("texture_opacity")).isDouble()) {
            assignError(error_message, QStringLiteral("texture_opacity must be a number from 0 to 1."));
            return std::nullopt;
        }
        const double opacity = root.value(QStringLiteral("texture_opacity")).toDouble();
        if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
            assignError(error_message, QStringLiteral("texture_opacity must be between 0 and 1."));
            return std::nullopt;
        }
        theme.textures.opacity = std::min(opacity, kMaxTextureOpacity);
    }

    if (root.contains(QStringLiteral("texture_mode"))) {
        if (!root.value(QStringLiteral("texture_mode")).isString()) {
            assignError(error_message, QStringLiteral("texture_mode must be tile, stretch, or cover."));
            return std::nullopt;
        }
        const QString mode = root.value(QStringLiteral("texture_mode")).toString();
        if (mode.compare(u"tile", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Tile;
        } else if (mode.compare(u"stretch", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Stretch;
        } else if (mode.compare(u"cover", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Cover;
        } else {
            assignError(error_message, QStringLiteral("texture_mode must be tile, stretch, or cover."));
            return std::nullopt;
        }
    }

    if (enforce_contrast
        && !validatePanelTextureContrast(
            theme, base_path, error_message, validated_panel_pixels)) {
        return std::nullopt;
    }

    return theme;
}

// ─── loadBuiltin ──────────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::loadBuiltin(const QString &resource_prefix)
{
    const auto json_path = resource_prefix + QStringLiteral("/theme.json");
    QFile f(json_path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SkinLoader: cannot open built-in skin:" << json_path;
        return std::nullopt;
    }

    QString error_message;
    auto theme = parseThemeJson(f.readAll(), resource_prefix, &error_message, true);
    if (!theme) {
        qWarning() << "SkinLoader: invalid built-in skin" << resource_prefix << error_message;
    }
    if (theme) {
        theme->builtin = true;
    }
    return theme;
}

// ─── loadFromDirectory ────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::loadFromDirectory(const QString &skin_dir)
{
    // Installed-skin reads share the process-wide transaction boundary with
    // imports, so callers never observe a replacement between its two renames.
    QMutexLocker transaction_lock(&skinCatalogMutex());
    const auto json_path = skin_dir + QStringLiteral("/theme.json");
    QFile f(json_path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SkinLoader: cannot open" << json_path;
        return std::nullopt;
    }

    if (f.size() <= 0 || f.size() > kMaxThemeBytes) {
        qWarning() << "SkinLoader: theme.json is empty or exceeds 256 KiB:" << json_path;
        return std::nullopt;
    }

    QString error_message;
    quint64 validated_panel_pixels = 0;
    auto theme = parseThemeJson(
        f.readAll(), skin_dir, &error_message, true, &validated_panel_pixels);
    if (theme && !validateInstalledThemeImages(
            *theme, skin_dir, validated_panel_pixels, error_message)) {
        theme.reset();
    }
    if (!theme) {
        qWarning() << "SkinLoader: invalid custom skin" << skin_dir << error_message;
    }
    if (theme) {
        theme->builtin = false;
    }
    return theme;
}

// ─── importArchive ────────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::importArchive(const QString &archive_path,
                                                    const QString &dest_dir)
{
    auto result = importArchiveDetailed(archive_path, dest_dir, false);
    if (!result.succeeded()) {
        qWarning() << "SkinLoader: import failed:" << result.error_message;
        return std::nullopt;
    }
    return result.theme;
}

SkinImportResult SkinLoader::importArchiveDetailed(
    const QString &archive_path,
    const QString &dest_dir,
    bool replace_existing) {
    // Settings windows are delete-on-close, but their QtConcurrent jobs keep
    // running. Serialize the complete import operation at the process boundary
    // so old and newly-opened windows cannot overlap replacement transactions.
    // Archive validation may be expensive, so serialize it only against other
    // import jobs. Installed-skin readers use the narrower catalogue lock below.
    QMutexLocker import_lock(&skinImportMutex());
    SkinImportResult result;

    const QFileInfo archive_info(archive_path);
    const QString suffix = archive_info.suffix();
    if (suffix.compare(u"cosmo", Qt::CaseInsensitive) != 0
        && suffix.compare(u"zip", Qt::CaseInsensitive) != 0) {
        result.error_message = QStringLiteral("Select a .cosmo or .zip skin archive.");
        return result;
    }

    QString skin_name = archive_info.completeBaseName().normalized(QString::NormalizationForm_KC);
    skin_name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    skin_name.remove(QRegularExpression(QStringLiteral("^[._-]+")));
    skin_name.remove(QRegularExpression(QStringLiteral(R"([.]+$)")));
    skin_name.truncate(64);
    if (skin_name.isEmpty()) {
        skin_name = QStringLiteral("skin");
    }
    static const QRegularExpression windows_device_name(
        QStringLiteral(R"(^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (windows_device_name.match(skin_name).hasMatch()) {
        skin_name.prepend(QStringLiteral("skin_"));
        skin_name.truncate(64);
    }

    QString archive_error;
    auto archive = detail::readSkinArchive(archive_path, archive_error);
    if (!archive) {
        result.error_message = archive_error;
        return result;
    }

    quint64 decoded_pixel_total = 0;
    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (it.key().endsWith(u".png", Qt::CaseInsensitive)) {
            quint64 image_pixels = 0;
            if (!validatePng(it.value(), it.key(), image_pixels, result.error_message)) {
                return result;
            }
            decoded_pixel_total += image_pixels;
            if (decoded_pixel_total > kMaxDecodedImagePixels) {
                result.error_message = QStringLiteral(
                    "Skin images exceed the 20-megapixel decoded-image budget.");
                return result;
            }
        }
    }

    if (!QDir().mkpath(dest_dir)) {
        result.error_message = QStringLiteral("Could not create the custom-skins directory.");
        return result;
    }
    const QFileInfo destination_info(dest_dir);
    if (!destination_info.isDir() || destination_info.isSymLink()) {
        result.error_message = QStringLiteral("The custom-skins destination is not a safe directory.");
        return result;
    }

    const QDir destination(dest_dir);
    QStringList case_insensitive_matches;
    const auto destination_entries = destination.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &entry : destination_entries) {
        if (entry.compare(skin_name, Qt::CaseInsensitive) == 0) {
            case_insensitive_matches.append(entry);
        }
    }
    if (case_insensitive_matches.size() > 1) {
        result.error_message = QStringLiteral(
            "Multiple installed skins differ only by letter case; resolve them before importing '%1'.")
            .arg(skin_name);
        return result;
    }
    const QString storage_name = case_insensitive_matches.isEmpty()
        ? skin_name
        : case_insensitive_matches.constFirst();
    const QString target_dir = destination.filePath(storage_name);
    result.target_path = target_dir;

    QTemporaryDir staging(
        QDir(dest_dir).filePath(QStringLiteral(".cosmoskin-import-XXXXXX")));
    if (!staging.isValid()) {
        result.error_message = QStringLiteral("Could not create a temporary skin staging directory.");
        return result;
    }
    const QString staged_skin = QDir(staging.path()).filePath(QStringLiteral("skin"));
    if (!QDir().mkpath(staged_skin)) {
        result.error_message = QStringLiteral("Could not initialize the skin staging directory.");
        return result;
    }

    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (!writeStagedFile(staged_skin, it.key(), it.value(), result.error_message)) {
            return result;
        }
    }

    QString parse_error;
    auto staged_theme = parseThemeJson(
        archive->files.value(QStringLiteral("theme.json")), staged_skin, &parse_error);
    if (!staged_theme) {
        result.error_message = parse_error;
        return result;
    }

    QSet<QString> referenced_textures;
    for (const QString &texture_path : staged_theme->textures.paths) {
        referenced_textures.insert(QDir(staged_skin).relativeFilePath(texture_path));
    }
    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("textures/"))
            && !referenced_textures.contains(it.key())) {
            result.error_message = QStringLiteral(
                "Texture %1 is present but is not referenced by theme.json.").arg(it.key());
            return result;
        }
    }

    // From this point through final verification, catalogue readers must see
    // either the complete old installation or the complete replacement.
    QMutexLocker catalog_lock(&skinCatalogMutex());
    const QFileInfo existing_target(target_dir);
    if (existing_target.exists() && !replace_existing) {
        result.status = SkinImportStatus::AlreadyExists;
        result.error_message = QStringLiteral("A skin named '%1' is already installed.").arg(storage_name);
        return result;
    }
    if (existing_target.exists()
        && (!existing_target.isDir() || existing_target.isSymLink())) {
        result.error_message = QStringLiteral(
            "The existing skin target is not a replaceable directory: %1").arg(target_dir);
        return result;
    }

    QString backup_dir;
    if (existing_target.exists()) {
        backup_dir = QDir(dest_dir).filePath(
            QStringLiteral(".cosmoskin-backup-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().rename(target_dir, backup_dir)) {
            result.error_message = QStringLiteral("Could not stage the existing skin for replacement.");
            return result;
        }
    }

    if (!QDir().rename(staged_skin, target_dir)) {
        if (!backup_dir.isEmpty() && !QDir().rename(backup_dir, target_dir)) {
            qCritical() << "SkinLoader: could not roll back skin replacement" << target_dir;
        }
        result.error_message = QStringLiteral("Could not install the validated skin.");
        return result;
    }

    auto installed_theme = loadFromDirectory(target_dir);
    if (!installed_theme) {
        QDir(target_dir).removeRecursively();
        if (!backup_dir.isEmpty() && !QDir().rename(backup_dir, target_dir)) {
            qCritical() << "SkinLoader: could not restore skin after verification failure" << target_dir;
        }
        result.error_message = QStringLiteral("The installed skin failed final verification.");
        return result;
    }

    if (!backup_dir.isEmpty() && !QDir(backup_dir).removeRecursively()) {
        qWarning() << "SkinLoader: imported skin but could not remove backup" << backup_dir;
    }

    installed_theme->id = QStringLiteral("custom:") + storage_name;
    result.status = SkinImportStatus::Imported;
    result.theme = std::move(installed_theme);
    result.error_message.clear();
    return result;
}

// ─── discoverAll ──────────────────────────────────────────────────────────────

QList<CosmoTheme> SkinLoader::discoverAll(const QString &custom_skins_dir)
{
    QMutexLocker transaction_lock(&skinCatalogMutex());
    QList<CosmoTheme> result;

    // Built-in skins
    if (auto dark = loadBuiltin(QStringLiteral(":/skins/dark"))) {
        dark->id = QStringLiteral("builtin:dark");
        result.append(*dark);
    }
    if (auto light = loadBuiltin(QStringLiteral(":/skins/light"))) {
        light->id = QStringLiteral("builtin:light");
        result.append(*light);
    }

    // Custom skins from disk
    QDir dir(custom_skins_dir);
    if (dir.exists()) {
        const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            const auto skin_path = custom_skins_dir + QStringLiteral("/") + entry;
            if (auto t = loadFromDirectory(skin_path)) {
                t->id = QStringLiteral("custom:") + entry;
                result.append(*t);
            }
        }
    }

    return result;
}
