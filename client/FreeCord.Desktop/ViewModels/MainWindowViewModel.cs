using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace FreeCord.Desktop.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    private readonly ServerListStore _store = new();

    public ObservableCollection<ServerSessionViewModel> Servers { get; } = new();

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasActiveServer))]
    private ServerSessionViewModel? _activeServer;

    // Плейсхолдер в правой панели виден, пока не выбран/не добавлен ни один сервер
    public bool HasActiveServer => ActiveServer is not null;

    // Открытая панель настроек сервера — оверлей внутри MainWindow (не отдельное окно,
    // см. ServerSettingsPanel), поэтому состояние "что показать и открыт ли оверлей
    // вообще" естественно живёт здесь, а не во ViewModel конкретной сессии.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsServerSettingsOpen))]
    private ServerSettingsViewModel? _activeServerSettings;

    public bool IsServerSettingsOpen => ActiveServerSettings is not null;

    public MainWindowViewModel()
    {
        // Один и тот же host:port мог попасть в servers.json дважды из-за старого
        // бага (не было проверки на дубликат при логине) — чистим при загрузке.
        var loaded = _store.Load();
        var deduped = new List<ServerEntry>();
        foreach (var entry in loaded)
        {
            if (deduped.Any(e => IsSameServer(e.Host, e.Port, entry.Host, entry.Port))) continue;
            deduped.Add(entry);
        }
        if (deduped.Count != loaded.Count) _store.Save(deduped);

        foreach (var entry in deduped)
            Servers.Add(CreateSession(entry.Host, entry.Port, entry.DisplayName, isPersisted: true));

        ActiveServer = Servers.FirstOrDefault();
    }

    // Единая точка создания сессии — чтобы не забыть подписаться на её события
    // (сейчас только ServerSettingsRequested) в обоих местах, где сессия появляется.
    private ServerSessionViewModel CreateSession(string host, int port, string displayName, bool isPersisted)
    {
        var session = new ServerSessionViewModel(_store, host, port, displayName, isPersisted, GuardDuplicate, GuardFingerprint);
        session.ServerSettingsRequested += server =>
            ActiveServerSettings = new ServerSettingsViewModel(server, onClose: () => ActiveServerSettings = null);
        return session;
    }

    // Клик по "+" в левой панели — новый, ещё не сохранённый сервер. DisplayName —
    // временная заглушка до подключения: имя теперь глобальное и приходит с сервера
    // (ServerInfoResponse, см. ServerSessionViewModel), локального переименования нет.
    [RelayCommand]
    private void AddServer()
    {
        const string host = "127.0.0.1";
        const int port = 6000;
        var session = CreateSession(host, port, $"{host}:{port}", isPersisted: false);
        Servers.Add(session);
        ActiveServer = session;
    }

    // Вызывается сессией перед подключением к сети. Дешёвая проверка по строке
    // host:port — если такой адрес уже занят другой вкладкой, не даём создать
    // вторую независимую сессию, а переключаемся на уже существующую.
    // Не ловит случай, когда один и тот же сервер доступен под разными адресами
    // (например, 127.0.0.1 и 127.0.0.2 на loopback) — для этого см. GuardFingerprint.
    private bool GuardDuplicate(ServerSessionViewModel candidate)
    {
        var other = Servers.FirstOrDefault(s =>
            s != candidate && IsSameServer(s.Host, s.Port, candidate.Host, candidate.Port));
        return ResolveCollision(candidate, other);
    }

    // Вызывается сессией сразу после успешного TLS-подключения, до логина/регистрации.
    // Один физический сервер может отвечать под разными адресами — а сертификат
    // (и, значит, отпечаток) у него всегда один, поэтому это надёжнее сравнения строк.
    private bool GuardFingerprint(ServerSessionViewModel candidate)
    {
        var fingerprint = candidate.ServerFingerprint;
        if (fingerprint is null) return true;

        var other = Servers.FirstOrDefault(s => s != candidate && s.ServerFingerprint == fingerprint);
        return ResolveCollision(candidate, other);
    }

    // Общая точка для обеих проверок. ВАЖНО: выживает не тот, кто оказался
    // "существующим" в момент проверки (это зависело бы от того, кто раньше
    // успел подключиться по сети — а после перезапуска приложения сохранённая
    // на диске вкладка как раз ещё не подключена, и тогда свежедобавленная
    // "+"-вкладка ошибочно побеждала бы настоящую), а тот, у кого выше приоритет:
    // сохранённый на диске сервер всегда важнее свежедобавленного, при прочих
    // равных — тот, кто раньше в списке (устойчиво к порядку логина/подключения).
    private bool ResolveCollision(ServerSessionViewModel candidate, ServerSessionViewModel? other)
    {
        if (other is null) return true;

        if (ShouldSurvive(candidate, other))
        {
            DiscardDuplicate(other, candidate);
            return true;
        }

        DiscardDuplicate(candidate, other);
        return false;
    }

    private bool ShouldSurvive(ServerSessionViewModel a, ServerSessionViewModel b)
    {
        if (a.IsPersisted != b.IsPersisted) return a.IsPersisted;
        return Servers.IndexOf(a) < Servers.IndexOf(b);
    }

    private void DiscardDuplicate(ServerSessionViewModel discarded, ServerSessionViewModel survivor)
    {
        Servers.Remove(discarded);
        ActiveServer = survivor;
        survivor.Status = "Этот сервер уже добавлен";

        // Если отбрасываемая вкладка тоже была сохранена (два разных адреса одного
        // сервера, оба когда-то залогинены) — не оставлять от неё запись-призрак,
        // которая на следующем запуске снова спровоцирует эту же коллизию.
        if (discarded.IsPersisted)
        {
            var entries = _store.Load();
            entries.RemoveAll(e => IsSameServer(e.Host, e.Port, discarded.Host, discarded.Port));
            _store.Save(entries);
        }

        _ = discarded.DisconnectAsync();
    }

    private static bool IsSameServer(string hostA, int portA, string hostB, int portB) =>
        portA == portB && string.Equals(hostA.Trim(), hostB.Trim(), StringComparison.OrdinalIgnoreCase);

    // Пункты контекстного меню в рейле — порядок в UI совпадает с порядком в servers.json
    [RelayCommand]
    private void MoveServerUp(ServerSessionViewModel server) => MoveServer(server, -1);

    [RelayCommand]
    private void MoveServerDown(ServerSessionViewModel server) => MoveServer(server, 1);

    private void MoveServer(ServerSessionViewModel server, int delta)
    {
        int index = Servers.IndexOf(server);
        int newIndex = index + delta;
        if (index < 0 || newIndex < 0 || newIndex >= Servers.Count) return;

        Servers.Move(index, newIndex);
        PersistOrder();
    }

    private void PersistOrder()
    {
        var persisted = Servers.Where(s => s.IsPersisted)
            .Select(s => new ServerEntry(s.Host, s.Port, s.DisplayName))
            .ToList();
        _store.Save(persisted);
    }

    [RelayCommand]
    private async Task RemoveServerAsync(ServerSessionViewModel server)
    {
        if (!Servers.Remove(server)) return;

        if (ActiveServer == server)
            ActiveServer = Servers.FirstOrDefault();

        if (server.IsPersisted)
        {
            var entries = _store.Load();
            entries.RemoveAll(e => IsSameServer(e.Host, e.Port, server.Host, server.Port));
            _store.Save(entries);
        }

        await server.DisconnectAsync();
    }

    // Буфер обмена — забота View (нужен TopLevel), MainWindow.axaml.cs подписывается
    // и сам достаёт TopLevel. Переименование сервера и загрузка его иконки отсюда
    // убраны — имя теперь глобальное (см. ServerSessionViewModel.DisplayName), а
    // иконку и настройки сервера открывают через кнопку-шестерёнку у Owner'а,
    // события которых уже живут прямо на ServerSessionViewModel (там же контекст).
    public event Action<ServerSessionViewModel>? CopyInviteLinkRequested;

    [RelayCommand]
    private void CopyInviteLink(ServerSessionViewModel server) => CopyInviteLinkRequested?.Invoke(server);
}
