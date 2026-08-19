using System.Drawing.Drawing2D;
using System.Drawing.Printing;

namespace CalendarPrintAssistant;

public sealed class MainForm : Form
{
    private readonly ComboBox printers = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox papers = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly RadioButton portrait = new() { Text = "纵向", Checked = true, AutoSize = true };
    private readonly RadioButton landscape = new() { Text = "横向", AutoSize = true };
    private readonly NumericUpDown widthCm = new() { Minimum = 1, Maximum = 100, DecimalPlaces = 2, Value = 21.00M, Increment = 0.10M };
    private readonly NumericUpDown heightCm = new() { Minimum = 1, Maximum = 100, DecimalPlaces = 2, Value = 21.00M, Increment = 0.10M };
    private readonly NumericUpDown copies = new() { Minimum = 1, Maximum = 999, Value = 1 };
    private readonly ComboBox position = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly CheckBox keepRatio = new() { Text = "保持比例", Checked = true, AutoSize = true };
    private readonly CheckBox crop = new() { Text = "允许裁切填满", AutoSize = true };
    private readonly Label fileLabel = new() { Text = "拖入图片，或点击“打开图片”", AutoSize = true };
    private readonly PreviewBox preview = new();
    private Image? currentImage;
    private string? currentPath;

    public MainForm()
    {
        Text = "日历打印助手 v0.1";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(1000, 680);
        Size = new Size(1160, 760);
        Font = new Font("Microsoft YaHei UI", 9F);
        AutoScaleMode = AutoScaleMode.Dpi;
        AllowDrop = true;

        BuildUi();
        LoadPrinters();
        position.Items.AddRange(new object[] { "顶部居中", "页面居中", "左上角" });
        position.SelectedIndex = 0;

        printers.SelectedIndexChanged += (_, _) => LoadPapers();
        portrait.CheckedChanged += (_, _) => preview.Invalidate();
        landscape.CheckedChanged += (_, _) => preview.Invalidate();
        widthCm.ValueChanged += (_, _) => preview.Invalidate();
        heightCm.ValueChanged += (_, _) => preview.Invalidate();
        position.SelectedIndexChanged += (_, _) => preview.Invalidate();
        crop.CheckedChanged += (_, _) => preview.Invalidate();

        preview.StateProvider = GetPreviewState;
        DragEnter += (_, e) => { if (e.Data?.GetDataPresent(DataFormats.FileDrop) == true) e.Effect = DragDropEffects.Copy; };
        DragDrop += (_, e) => { if (e.Data?.GetData(DataFormats.FileDrop) is string[] f && f.Length > 0) LoadImage(f[0]); };
        FormClosed += (_, _) => currentImage?.Dispose();
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 2, RowCount = 1 };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 68));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 32));
        Controls.Add(root);

        var left = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2 };
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 58));
        left.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var topBar = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(0, 8, 0, 0) };
        var open = new Button { Text = "打开图片", Width = 100, Height = 32 };
        open.Click += (_, _) => OpenImage();
        fileLabel.Padding = new Padding(10, 8, 0, 0);
        topBar.Controls.Add(open);
        topBar.Controls.Add(fileLabel);
        preview.Dock = DockStyle.Fill;
        preview.BorderStyle = BorderStyle.FixedSingle;
        preview.BackColor = Color.FromArgb(245, 245, 245);
        left.Controls.Add(topBar, 0, 0);
        left.Controls.Add(preview, 0, 1);
        root.Controls.Add(left, 0, 0);

        var side = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(14), BackColor = Color.White, ColumnCount = 2, AutoScroll = true };
        side.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 95));
        side.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        int r = 0;
        AddHeading(side, "打印设置", ref r);
        AddRow(side, "打印机", printers, ref r);
        AddRow(side, "纸张", papers, ref r);
        var dir = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        dir.Controls.Add(portrait); dir.Controls.Add(landscape);
        AddRow(side, "方向", dir, ref r);
        AddHeading(side, "21×21cm 日历", ref r);
        var size = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        widthCm.Width = 78; heightCm.Width = 78;
        size.Controls.Add(widthCm); size.Controls.Add(new Label { Text = "×", AutoSize = true, Padding = new Padding(3, 6, 3, 0) }); size.Controls.Add(heightCm);
        AddRow(side, "尺寸(cm)", size, ref r);
        AddRow(side, "位置", position, ref r);
        var opts = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        opts.Controls.Add(keepRatio); opts.Controls.Add(crop);
        AddRow(side, "选项", opts, ref r);
        AddRow(side, "份数", copies, ref r);

        var props = new Button { Text = "打印机属性", Height = 32, Dock = DockStyle.Top };
        props.Click += (_, _) => ShowPrinterDialog(false);
        AddRow(side, "", props, ref r);
        var pp = new Button { Text = "打印预览", Height = 34, Dock = DockStyle.Top };
        pp.Click += (_, _) => ShowPreview();
        AddRow(side, "", pp, ref r);
        var print = new Button { Text = "打印", Height = 40, Dock = DockStyle.Top, Font = new Font(Font, FontStyle.Bold) };
        print.Click += (_, _) => PrintNow();
        AddRow(side, "", print, ref r);
        var note = new Label { AutoSize = true, MaximumSize = new Size(300, 0), ForeColor = Color.DimGray, Text = "建议：A4纵向、21.00×21.00cm。Canon 无边框请在“打印机属性”里启用。" };
        AddRow(side, "", note, ref r);
        root.Controls.Add(side, 1, 0);
    }

    private static void AddHeading(TableLayoutPanel t, string text, ref int r)
    {
        var l = new Label { Text = text, AutoSize = true, Font = new Font("Microsoft YaHei UI", 10F, FontStyle.Bold), Padding = new Padding(0, 10, 0, 8) };
        t.Controls.Add(l, 0, r); t.SetColumnSpan(l, 2); r++;
    }

    private static void AddRow(TableLayoutPanel t, string name, Control c, ref int r)
    {
        var l = new Label { Text = name, AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 8, 0, 5) };
        c.Anchor = AnchorStyles.Left | AnchorStyles.Right;
        c.Margin = new Padding(3, 4, 3, 4);
        t.Controls.Add(l, 0, r); t.Controls.Add(c, 1, r); r++;
    }

    private void LoadPrinters()
    {
        foreach (string p in PrinterSettings.InstalledPrinters) printers.Items.Add(p);
        using var ps = new PrinterSettings();
        if (printers.Items.Contains(ps.PrinterName)) printers.SelectedItem = ps.PrinterName;
        else if (printers.Items.Count > 0) printers.SelectedIndex = 0;
    }

    private void LoadPapers()
    {
        papers.Items.Clear();
        if (printers.SelectedItem is not string name) return;
        try
        {
            var ps = new PrinterSettings { PrinterName = name };
            int a4 = -1;
            foreach (PaperSize p in ps.PaperSizes)
            {
                papers.Items.Add(new PaperItem(p));
                if (a4 < 0 && (p.Kind == PaperKind.A4 || p.PaperName.Contains("A4", StringComparison.OrdinalIgnoreCase))) a4 = papers.Items.Count - 1;
            }
            if (papers.Items.Count > 0) papers.SelectedIndex = a4 >= 0 ? a4 : 0;
        }
        catch { }
        preview.Invalidate();
    }

    private void OpenImage()
    {
        using var d = new OpenFileDialog { Filter = "图片|*.jpg;*.jpeg;*.png;*.bmp;*.gif|所有文件|*.*" };
        if (d.ShowDialog(this) == DialogResult.OK) LoadImage(d.FileName);
    }

    private void LoadImage(string path)
    {
        try
        {
            using var tmp = Image.FromFile(path);
            var bmp = new Bitmap(tmp);
            currentImage?.Dispose(); currentImage = bmp; currentPath = path;
            fileLabel.Text = $"{Path.GetFileName(path)}   {bmp.Width}×{bmp.Height}px";
            preview.Invalidate();
        }
        catch (Exception ex) { MessageBox.Show("图片打开失败：" + ex.Message); }
    }

    private PrintDocument CreateDocument()
    {
        if (printers.SelectedItem is not string printerName) throw new InvalidOperationException("没有可用打印机");
        var doc = new PrintDocument();
        doc.PrinterSettings.PrinterName = printerName;
        doc.PrinterSettings.Copies = (short)copies.Value;
        if (papers.SelectedItem is PaperItem pi) doc.DefaultPageSettings.PaperSize = pi.Size;
        doc.DefaultPageSettings.Landscape = landscape.Checked;
        doc.OriginAtMargins = false;
        doc.PrintPage += PrintPage;
        return doc;
    }

    private void PrintNow()
    {
        if (currentImage == null) { MessageBox.Show("请先打开图片"); return; }
        try
        {
            using var doc = CreateDocument();
            using var d = new PrintDialog { Document = doc, UseEXDialog = true, AllowPrintToFile = false };
            if (d.ShowDialog(this) == DialogResult.OK) doc.Print();
        }
        catch (Exception ex) { MessageBox.Show("打印失败：" + ex.Message); }
    }

    private void ShowPreview()
    {
        if (currentImage == null) { MessageBox.Show("请先打开图片"); return; }
        try
        {
            using var doc = CreateDocument();
            using var p = new PrintPreviewDialog { Document = doc, Width = 1000, Height = 760, StartPosition = FormStartPosition.CenterParent };
            p.ShowDialog(this);
        }
        catch (Exception ex) { MessageBox.Show("预览失败：" + ex.Message); }
    }

    private void ShowPrinterDialog(bool print)
    {
        try
        {
            using var doc = CreateDocument();
            using var d = new PrintDialog { Document = doc, UseEXDialog = true };
            if (d.ShowDialog(this) == DialogResult.OK && print) doc.Print();
        }
        catch (Exception ex) { MessageBox.Show("无法打开打印设置：" + ex.Message); }
    }

    private void PrintPage(object? sender, PrintPageEventArgs e)
    {
        if (currentImage == null) return;
        e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        float w = (float)widthCm.Value / 2.54f * 100f;
        float h = (float)heightCm.Value / 2.54f * 100f;
        float pageW = e.PageBounds.Width, pageH = e.PageBounds.Height;
        float x = position.SelectedIndex switch { 0 => (pageW - w) / 2f, 1 => (pageW - w) / 2f, _ => 0f };
        float y = position.SelectedIndex switch { 1 => (pageH - h) / 2f, _ => 0f };
        DrawSmart(e.Graphics, currentImage, new RectangleF(x, y, w, h), crop.Checked);
        e.HasMorePages = false;
    }

    private PreviewState GetPreviewState()
    {
        float pw = 210, ph = 297;
        if (papers.SelectedItem is PaperItem p)
        {
            pw = p.Size.Width / 100f * 25.4f; ph = p.Size.Height / 100f * 25.4f;
        }
        if (landscape.Checked) (pw, ph) = (ph, pw);
        return new PreviewState(currentImage, pw, ph, (float)widthCm.Value * 10f, (float)heightCm.Value * 10f, position.SelectedIndex, crop.Checked);
    }

    private static void DrawSmart(Graphics g, Image img, RectangleF dest, bool doCrop)
    {
        if (!doCrop)
        {
            float s = Math.Min(dest.Width / img.Width, dest.Height / img.Height);
            float w = img.Width * s, h = img.Height * s;
            g.DrawImage(img, new RectangleF(dest.X + (dest.Width - w) / 2f, dest.Y + (dest.Height - h) / 2f, w, h));
            return;
        }
        float sa = img.Width / (float)img.Height, da = dest.Width / dest.Height;
        RectangleF src;
        if (sa > da) { float sw = img.Height * da; src = new RectangleF((img.Width - sw) / 2f, 0, sw, img.Height); }
        else { float sh = img.Width / da; src = new RectangleF(0, (img.Height - sh) / 2f, img.Width, sh); }
        g.DrawImage(img, dest, src, GraphicsUnit.Pixel);
    }

    private sealed class PaperItem
    {
        public PaperSize Size { get; }
        public PaperItem(PaperSize size) => Size = size;
        public override string ToString() => Size.PaperName;
    }

    private sealed record PreviewState(Image? Image, float PaperWmm, float PaperHmm, float ImgWmm, float ImgHmm, int Position, bool Crop);

    private sealed class PreviewBox : Panel
    {
        public Func<PreviewState>? StateProvider { get; set; }
        public PreviewBox() { DoubleBuffered = true; ResizeRedraw = true; }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            if (StateProvider == null) return;
            var s = StateProvider();
            float m = 30;
            float scale = Math.Min((ClientSize.Width - 2 * m) / s.PaperWmm, (ClientSize.Height - 2 * m) / s.PaperHmm);
            float pw = s.PaperWmm * scale, ph = s.PaperHmm * scale;
            float px = (ClientSize.Width - pw) / 2f, py = (ClientSize.Height - ph) / 2f;
            e.Graphics.FillRectangle(Brushes.LightGray, px + 6, py + 6, pw, ph);
            e.Graphics.FillRectangle(Brushes.White, px, py, pw, ph);
            e.Graphics.DrawRectangle(Pens.Silver, px, py, pw, ph);
            if (s.Image == null) return;
            float x = s.Position switch { 0 => (s.PaperWmm - s.ImgWmm) / 2f, 1 => (s.PaperWmm - s.ImgWmm) / 2f, _ => 0f };
            float y = s.Position == 1 ? (s.PaperHmm - s.ImgHmm) / 2f : 0f;
            var d = new RectangleF(px + x * scale, py + y * scale, s.ImgWmm * scale, s.ImgHmm * scale);
            DrawSmart(e.Graphics, s.Image, d, s.Crop);
            using var pen = new Pen(Color.DodgerBlue, 2);
            e.Graphics.DrawRectangle(pen, d.X, d.Y, d.Width, d.Height);
        }
    }
}
