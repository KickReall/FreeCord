using System.Text;

namespace FreeCord.Protocol;

/// <summary>Запись payload в том же формате, что C++ Serialization.h</summary>
public sealed class PayloadWriter : IDisposable
{
	private readonly MemoryStream _ms = new();
	private readonly BinaryWriter _w;

	public PayloadWriter() => _w = new BinaryWriter(_ms, Encoding.UTF8, leaveOpen: true);

	public void WriteByte(byte v) => _w.Write(v);
	public void WriteUInt32(uint v) => _w.Write(v);
	public void WriteUInt64(ulong v) => _w.Write(v);
	public void WriteInt64(long v) => _w.Write(v);

	/// <summary>
	/// ВАЖНО: не используем BinaryWriter.Write(string) — он пишет длину
	/// в формате 7-bit encoded int, а C++ ждёт ровно uint16.
	/// </summary>
	public void WriteString(string s)
	{
		var bytes = Encoding.UTF8.GetBytes(s);
		if (bytes.Length > ushort.MaxValue)
			throw new ArgumentException("String too long for uint16 length prefix");
		_w.Write((ushort)bytes.Length);
		_w.Write(bytes);
	}

	/// <summary>uint32 длина + байты — для бинарных данных вроде аватарок (WriteString ограничен 64 КБ).</summary>
	public void WriteBytes(byte[] data)
	{
		_w.Write((uint)data.Length);
		_w.Write(data);
	}

	public byte[] ToArray()
	{
		_w.Flush();
		return _ms.ToArray();
	}

	public void Dispose()
	{
		_w.Dispose();
		_ms.Dispose();
	}
}

/// <summary>Чтение payload, зеркало PayloadWriter</summary>
public sealed class PayloadReader : IDisposable
{
	private readonly MemoryStream _ms;
	private readonly BinaryReader _r;

	public PayloadReader(byte[] data)
	{
		_ms = new MemoryStream(data);
		_r = new BinaryReader(_ms, Encoding.UTF8, leaveOpen: true);
	}

	public byte ReadByte() => _r.ReadByte();
	public uint ReadUInt32() => _r.ReadUInt32();
	public ulong ReadUInt64() => _r.ReadUInt64();
	public long ReadInt64() => _r.ReadInt64();

	public string ReadString()
	{
		ushort len = _r.ReadUInt16();
		var bytes = _r.ReadBytes(len);
		return Encoding.UTF8.GetString(bytes);
	}

	public byte[] ReadByteArray()
	{
		uint len = _r.ReadUInt32();
		return _r.ReadBytes((int)len);
	}

	public void Dispose()
	{
		_r.Dispose();
		_ms.Dispose();
	}
}