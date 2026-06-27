using System;
using System.IO;
using System.Runtime.InteropServices;

class TestDecoder
{
    static void Main()
    {
        var clsid = new Guid("11E7785D-7BFE-411C-AD88-48849C9EE8B1");
        var type = Type.GetTypeFromCLSID(clsid);
        var decoder = (IWICBitmapDecoder)Activator.CreateInstance(type);

        string path = @"C:\Users\user\Downloads\discord-bot\utils\crypto\bitcoin-btc-logo.svg";
        var fs = new FileStream(path, FileMode.Open, FileAccess.Read);

        // Create an IStream wrapper
        var stream = new IStreamWrapper(fs);

        // Test QueryCapability
        uint cap = 0;
        int hr = decoder.QueryCapability(stream, ref cap);
        Console.WriteLine("QueryCapability hr=0x{0:X} cap=0x{1:X}", hr, cap);

        // Test Initialize
        hr = decoder.Initialize(stream, 0);
        Console.WriteLine("Initialize hr=0x{0:X}", hr);

        // Get container format
        Guid fmt;
        decoder.GetContainerFormat(out fmt);
        Console.WriteLine("ContainerFormat: {0}", fmt);

        // Get frame count
        uint count = 0;
        decoder.GetFrameCount(ref count);
        Console.WriteLine("Frame count: {0}", count);

        // Get first frame
        IWICBitmapFrameDecode frame;
        decoder.GetFrame(0, out frame);
        uint w, h;
        frame.GetSize(out w, out h);
        Console.WriteLine("Frame size: {0}x{1}", w, h);

        // Copy pixels
        uint stride = w * 4;
        byte[] pixels = new byte[stride * h];
        WICRect rect = new WICRect { X = 0, Y = 0, Width = (int)w, Height = (int)h };
        frame.CopyPixels(ref rect, stride, (uint)pixels.Length, pixels);
        Console.WriteLine("Pixels copied: {0} bytes", pixels.Length);

        // Check if pixels are non-zero (has content)
        bool hasContent = false;
        foreach (byte b in pixels) { if (b != 0) { hasContent = true; break; } }
        Console.WriteLine("Has content: {0}", hasContent);

        // Save as BMP for verification
        SaveAsBmp(path.Replace(".svg", "_wic.bmp"), pixels, (int)w, (int)h);
        Console.WriteLine("Saved: " + path.Replace(".svg", "_wic.bmp"));

        Console.WriteLine("SUCCESS: WIC decoder works!");
    }

    static void SaveAsBmp(string path, byte[] pixels, int w, int h)
    {
        int stride = w * 4;
        int pad = (4 - (w * 3) % 4) % 4;
        int dataSize = (w * 3 + pad) * h;
        int fileSize = 14 + 40 + dataSize;

        using (var fs = new FileStream(path, FileMode.Create))
        {
            var bw = new BinaryWriter(fs);
            // BMP header
            bw.Write((byte)'B'); bw.Write((byte)'M');
            bw.Write(fileSize);
            bw.Write((short)0); bw.Write((short)0);
            bw.Write(14 + 40);
            // DIB header
            bw.Write(40); bw.Write(w); bw.Write(h);
            bw.Write((short)1); bw.Write((short)24);
            bw.Write(0); bw.Write(dataSize);
            bw.Write(2835); bw.Write(2835);
            bw.Write(0); bw.Write(0);
            // Pixel data (bottom-up, BGR)
            for (int y = h - 1; y >= 0; y--)
            {
                for (int x = 0; x < w; x++)
                {
                    int off = y * stride + x * 4;
                    bw.Write(pixels[off + 2]); // B
                    bw.Write(pixels[off + 1]); // G
                    bw.Write(pixels[off + 0]); // R
                }
                for (int p = 0; p < pad; p++) bw.Write((byte)0);
            }
        }
    }
}

// COM interfaces
[Guid("00000000-0000-0000-C000-000000000046")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IUnknown { }

[Guid("0000000c-0000-0000-C000-000000000046")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IStream
{
    void Read([Out] byte[] pv, uint cb, out uint pcbRead);
    void Write(byte[] pv, uint cb, out uint pcbWritten);
    void Seek(long dlibMove, uint dwOrigin, out long plibNewPosition);
    void SetSize(long libNewSize);
    void CopyTo(IStream pstm, long cb, out long pcbRead, out long pcbWritten);
    void Commit(uint grfCommitFlags);
    void Revert();
    void LockRegion(long libOffset, long cb, uint dwLockType);
    void UnlockRegion(long libOffset, long cb, uint dwLockType);
    void Stat(out STATSTG pstatstg, uint grfStatFlag);
    void Clone(out IStream ppstm);
}

[StructLayout(LayoutKind.Sequential)]
struct STATSTG
{
    public IntPtr pwcsName;
    public uint type;
    public long cbSize;
    public long mtime;
    public long ctime;
    public long atime;
    public uint grfMode;
    public uint grfLocksSupported;
    public Guid clsid;
    public uint grfStateBits;
    public uint reserved;
}

[Guid("9EDDE9E7-8DEE-47ea-99DF-E6FAF2ED44BF")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IWICBitmapDecoder
{
    int QueryCapability(IStream pStream, ref uint pCapability);
    int Initialize(IStream pStream, uint cacheOptions);
    void GetContainerFormat(out Guid pContainerFormat);
    void GetDecoderInfo(IntPtr ppIDecoderInfo);
    void CopyPalette(IntPtr pIPalette);
    void GetMetadataQueryReader(IntPtr ppIQMetadataQueryReader);
    void GetPreview(IntPtr ppIBitmapSource);
    void GetColorContexts(uint cCount, IntPtr ppIColorContexts, ref uint pcActualCount);
    void GetThumbnail(IntPtr ppIThumbnail);
    void GetFrameCount(ref uint pCount);
    void GetFrame(uint index, out IWICBitmapFrameDecode ppIBitmapFrame);
}

[Guid("3B16811B-6A43-4ec9-A813-3D930C13B940")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IWICBitmapFrameDecode
{
    // IWICBitmapSource methods
    void GetSize(out uint puiWidth, out uint puiHeight);
    void GetPixelFormat(out Guid pPixelFormat);
    void GetResolution(out double pDpiX, out double pDpiY);
    void CopyPixels(ref WICRect prc, uint cbStride, uint cbBufferSize, [Out] byte[] pbBuffer);
    // IWICBitmapFrameDecode methods
    void GetMetadataQueryReader(IntPtr ppIQMetadataQueryReader);
    void GetColorContexts(uint cCount, IntPtr ppIColorContexts, ref uint pcActualCount);
    void GetThumbnail(IntPtr ppIThumbnail);
}

[StructLayout(LayoutKind.Sequential)]
struct WICRect
{
    public int X, Y, Width, Height;
}

// IStream wrapper for .NET Stream
class IStreamWrapper : IStream
{
    FileStream _fs;
    public IStreamWrapper(FileStream fs) { _fs = fs; }

    public void Read(byte[] pv, uint cb, out uint pcbRead)
    {
        int r = _fs.Read(pv, 0, (int)cb);
        pcbRead = (uint)r;
    }
    public void Write(byte[] pv, uint cb, out uint pcbWritten)
    {
        _fs.Write(pv, 0, (int)cb);
        pcbWritten = cb;
    }
    public void Seek(long dlibMove, uint dwOrigin, out long plibNewPosition)
    {
        SeekOrigin origin = dwOrigin == 0 ? SeekOrigin.Begin : dwOrigin == 1 ? SeekOrigin.Current : SeekOrigin.End;
        plibNewPosition = _fs.Seek(dlibMove, origin);
    }
    public void SetSize(long libNewSize) { _fs.SetLength(libNewSize); }
    public void CopyTo(IStream pstm, long cb, out long pcbRead, out long pcbWritten)
    {
        pcbRead = 0; pcbWritten = 0;
        byte[] buf = new byte[8192];
        long total = 0;
        while (total < cb)
        {
            int toRead = (int)Math.Min(8192, cb - total);
            int r = _fs.Read(buf, 0, toRead);
            if (r == 0) break;
            uint written;
            pstm.Write(buf, (uint)r, out written);
            total += r; pcbRead += r; pcbWritten += written;
        }
    }
    public void Commit(uint grfCommitFlags) { _fs.Flush(); }
    public void Revert() { }
    public void LockRegion(long libOffset, long cb, uint dwLockType) { }
    public void UnlockRegion(long libOffset, long cb, uint dwLockType) { }
    public void Stat(out STATSTG pstatstg, uint grfStatFlag)
    {
        pstatstg = new STATSTG();
        pstatstg.cbSize = _fs.Length;
        pstatstg.type = 2; // STGTY_STREAM
    }
    public void Clone(out IStream ppstm) { ppstm = null; }
}
