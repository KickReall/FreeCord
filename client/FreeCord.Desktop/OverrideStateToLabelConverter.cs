using System;
using System.Globalization;
using Avalonia.Data.Converters;
using FreeCord.Desktop.ViewModels;

namespace FreeCord.Desktop;

/// <summary>Отображение OverrideState по-русски в выпадающем списке редактора
/// оверрайдов канала. Только в одну сторону — SelectedItem ComboBox синхронизируется
/// напрямую через сами значения enum, конвертер только про текст.</summary>
public sealed class OverrideStateToLabelConverter : IValueConverter
{
    public static readonly OverrideStateToLabelConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) => value switch
    {
        OverrideState.Allow => "Разрешить",
        OverrideState.Deny => "Запретить",
        _ => "Наследовать",
    };

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
