using System.Drawing.Drawing2D;
using System.Drawing.Printing;

namespace CalendarPrintAssistant;

public sealed class MainForm : Form
{
    private readonly ComboBox cmbPrinter = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox cmbPaper = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox cmbQuality = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox cmbLayout = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly ComboBox cmbPosition = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly RadioButton rbPortrait = new() { Text = "纵向", Checked = true, AutoSize = true };
    private readonly RadioButton rbLandscape = new() { Text = "横向", AutoSize = true };
    private readonly NumericUpDown nudCopies = new() { Minimum = 1, Maximum = 999, Value = 1, Width = 95 };
    private readonly CheckBox chkAutoRotate = new() { Text = "自动旋转以匹配纸张", Checked = true, AutoSize = true };
    private readonly CheckBox chkShowSafeArea = new() { Text = "显示安全区域", AutoSize = true };
    private readonly Label lblFile = new() { Text = "还没有添加图片", AutoEllipsis = true };
    private readonly Label lblImageInfo = new() { Text = "支持 JPG / PNG / BMP", ForeColor = Color.Gray, AutoSize = true };
    private readonly Label lblPaperInfo = new() { ForeColor = Color.Gray, AutoSize = true };
    private readonly PrintPreviewPanel preview = new();
    private Image? currentImage;
    private string? currentPath;
    private bool loadingPapers;

    private readonly Color accent = Color.FromArgb(64, 92, 255);
    private readonly Color surface = Color.White;
    private readonly Color canvas = Color.FromArgb(242, 244, 247);

    public MainForm()
    {
        Text = "日历打印助手 v0.2";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(1080, 700);
        Size = new Size(1280, 800);
        BackColor = canvas;
        Font = new Font("Microsoft YaHei UI", 9F);
        AutoScaleMode = AutoScaleMode.Dpi;
        AllowDrop = true;

        BuildUi();
        LoadPrinters();
        SetupDefaults();
        WireEvents();

        preview.StateProvider = GetPreviewState;
        FormClosed += (_, _) => currentImage?.Dispose();
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 1,
            Padding = new Padding(16),
            BackColor = canvas
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 72));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 28));
        Controls.Add(root);

        var leftCard = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = surface,
            Margin = new Padding(0, 0, 12, 0),
            Padding = new Padding(0)
        };
        root.Controls.Add(leftCard, 0, 0);

        var leftLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Padding = new Padding(14)
        };
        leftLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 64));
        leftLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        leftCard.Controls.Add(leftLayout);

        var toolbar = new Panel { Dock = DockStyle.Fill, BackColor = surface };
        leftLayout.Controls.Add(toolbar, 0, 0);

        var btnOpen = MakePrimaryButton("＋  添加图片", 112, 36);
        btnOpen.Location = new Point(0, 10);
        btnOpen.Click += (_, _) => OpenImage();
        toolbar.Controls.Add(btnOpen);

        lblFile.Location = new Point(128, 10);
        lblFile.Size = new Size(520, 22);
        lblFile.Font = new Font(Font, FontStyle.Bold);
        toolbar.Controls.Add(lblFile);

        lblImageInfo.Location = new Point(128, 34);
        toolbar.Controls.Add(lblImageInfo);

        preview.Dock = DockStyle.Fill;
        preview.BackColor = Color.FromArgb(235, 237, 241);
        leftLayout.Controls.Add(preview, 0, 1);

        var rightCard = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = surface,
            Padding = new Padding(18),
            AutoScroll = true
        };
        root.Controls.Add(rightCard, 1, 0);

        var settings = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 2,
            RowCount = 20,
            BackColor = surface
        };
        settings.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        settings.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        rightCard.Controls.Add(settings);

        int r = 0;
        AddTitle(settings, "打印设置", ref r);
        AddRow(settings, "打印机", cmbPrinter, ref r);

        var propButton = MakeSecondaryButton("打印机属性", 110, 30);
        propButton.Click += (_, _) => ShowPrinterProperties();
        AddRow(settings, "", propButton, ref r);

        AddDivider(settings, ref r);
        AddTitle(settings, "纸张与方向", ref r);
        AddRow(settings, "纸张大小", cmbPaper, ref r);
        AddRow(settings, "", lblPaperInfo, ref r);

        var dirPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = false };
        dirPanel.Controls.Add(rbPortrait);
        dirPanel.Controls.Add(rbLandscape);
        AddRow(settings, "方向", dirPanel, ref r);

        AddRow(settings, "打印质量", cmbQuality, ref r);

        AddDivider(settings, ref r);
        AddTitle(settings, "图片布局", ref r);
        AddRow(settings, "图片大小", cmbLayout, ref r);
        AddRow(settings, "图片位置", cmbPosition, ref r);

        var optionPanel = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false
        };
        optionPanel.Controls.Add(chkAutoRotate);
        optionPanel.Controls.Add(chkShowSafeArea);
        AddRow(settings, "选项", optionPanel, ref r);

        AddDivider(settings, ref r);
        AddTitle(settings, "打印", ref r);
        AddRow(settings, "打印份数", nudCopies, ref r);

        var tip = new Label
        {
            Text = "左侧就是最终排版预览。需要 Canon 无边框、照片纸类型等驱动专用参数时，点击“打印机属性”设置。",
            AutoSize = true,
            MaximumSize = new Size(280, 0),
            ForeColor = Color.FromArgb(105, 105, 105),
            Padding = new Padding(0, 8, 0, 8)
        };
        AddRow(settings, "", tip, ref r);

        var btnPrint = MakePrimaryButton("打印", 0, 46);
        btnPrint.Dock = DockStyle.Top;
        btnPrint.Font = new Font("Microsoft YaHei UI", 10F, FontStyle.Bold);
        btnPrint.Click += (_, _) => PrintNow();
        AddRow(settings, "", btnPrint, ref r);
    }

    private void SetupDefaults()
    {
        cmbQuality.Items.AddRange(new object[] { "高", "标准" });
        cmbQuality.SelectedIndex = 0;

        cmbLayout.Items.AddRange(new object[]
        {
            "铺满纸张（裁切边缘）",
            "适应纸张（完整显示）"
        });
        cmbLayout.SelectedIndex = 0;

        cmbPosition.Items.AddRange(new object[] { "页面居中", "顶部居中", "左上角" });
        cmbPosition.SelectedIndex = 0;
    }

    private void WireEvents()
    {
        cmbPrinter.SelectedIndexChanged += (_, _) => LoadPapers();
        cmbPaper.SelectedIndexChanged += (_, _) => { UpdatePaperInfo(); preview.Invalidate(); };
        cmbLayout.SelectedIndexChanged += (_, _) => preview.Invalidate();
        cmbPosition.SelectedIndexChanged += (_, _) => preview.Invalidate();
        cmbQuality.SelectedIndexChanged += (_, _) => preview.Invalidate();
        rbPortrait.CheckedChanged += (_, _) => preview.Invalidate();
        rbLandscape.CheckedChanged += (_, _) => preview.Invalidate();
        chkAutoRotate.CheckedChanged += (_, _) => preview.Invalidate();
        chkShowSafeArea.CheckedChanged += (_, _) => preview.Invalidate();

        DragEnter += (_, e) =>
        {
            if (e.Data?.GetDataPresent(DataFormats.FileDrop) == true)
                e.Effect = DragDropEffects.Copy;
        };
        DragDrop += (_, e) =>
        {
            if (e.Data?.GetData(DataFormats.FileDrop) is string[] files && files.Length > 0)
                LoadImage(files[0]);
        };
    }

    private Button MakePrimaryButton(string text, int width, int height)
    {
        return new Button
        {
            Text = text,
            Width = width,
            Height = height,
            BackColor = accent,
            ForeColor = Color.White,
            FlatStyle = FlatStyle.Flat,
            Cursor = Cursors.Hand,
            UseVisualStyleBackColor = false,
            FlatAppearance = { BorderSize = 0 }
        };
    }

    private static Button MakeSecondaryButton(string text, int width, int height)
    {
        return new Button
        {
            Text = text,
            Width = width,
            Height = height,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.White,
            Cursor = Cursors.Hand,
            FlatAppearance = { BorderColor = Color.FromArgb(205, 208, 215) }
        };
    }

    private static void AddTitle(TableLayoutPanel panel, string text, ref int row)
    {
        var label = new Label
        {
            Text = text,
            AutoSize = true,
            Font = new Font("Microsoft YaHei UI", 10F, FontStyle.Bold),
            Padding = new Padding(0, 8, 0, 8)
        };
        panel.Controls.Add(label, 0, row);
        panel.SetColumnSpan(label, 2);
        row++;
    }

    private static void AddDivider(TableLayoutPanel panel, ref int row)
    {
        var line = new Panel { Height = 1, Dock = DockStyle.Top, BackColor = Color.FromArgb(232, 234, 238), Margin = new Padding(0, 10, 0, 6) };
        panel.Controls.Add(line, 0, row);
        panel.SetColumnSpan(line, 2);
        row++;
    }

    private static void AddRow(TableLayoutPanel panel, string name, Control control, ref int row)
    {
        var label = new Label
        {
            Text = name,
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Padding = new Padding(0, 8, 0, 5)
        };
        control.Anchor = AnchorStyles.Left | AnchorStyles.Right;
        control.Margin = new Padding(3, 4, 3, 4);
        panel.Controls.Add(label, 0, row);
        panel.Controls.Add(control, 1, row);
        row++;
    }

    private void LoadPrinters()
    {
        cmbPrinter.Items.Clear();
        foreach (string p in PrinterSettings.InstalledPrinters)
            cmbPrinter.Items.Add(p);

        var settings = new PrinterSettings();
        if (!string.IsNullOrWhiteSpace(settings.PrinterName) && cmbPrinter.Items.Contains(settings.PrinterName))
            cmbPrinter.SelectedItem = settings.PrinterName;
        else if (cmbPrinter.Items.Count > 0)
            cmbPrinter.SelectedIndex = 0;

        if (cmbPrinter.Items.Count == 0)
        {
            cmbPrinter.Items.Add("未检测到打印机");
            cmbPrinter.SelectedIndex = 0;
            cmbPrinter.Enabled = false;
            LoadFallbackPapers();
        }
    }

    private void LoadPapers()
    {
        if (loadingPapers || cmbPrinter.SelectedItem is not string printerName || printerName == "未检测到打印机")
            return;

        loadingPapers = true;
        try
        {
            cmbPaper.Items.Clear();
            var ps = new PrinterSettings { PrinterName = printerName };
            int a4Index = -1;

            foreach (PaperSize p in ps.PaperSizes)
            {
                var item = new PaperItem(p);
                cmbPaper.Items.Add(item);
                if (a4Index < 0 && IsA4(p))
                    a4Index = cmbPaper.Items.Count - 1;
            }

            if (cmbPaper.Items.Count == 0)
            {
                LoadFallbackPapers();
                return;
            }

            cmbPaper.SelectedIndex = a4Index >= 0 ? a4Index : 0;
        }
        catch
        {
            LoadFallbackPapers();
        }
        finally
        {
            loadingPapers = false;
            UpdatePaperInfo();
            preview.Invalidate();
        }
    }

    private void LoadFallbackPapers()
    {
        cmbPaper.Items.Clear();
        cmbPaper.Items.Add(new PaperItem(new PaperSize("A4", 827, 1169)));
        cmbPaper.Items.Add(new PaperItem(new PaperSize("A5", 583, 827)));
        cmbPaper.Items.Add(new PaperItem(new PaperSize("Letter", 850, 1100)));
        cmbPaper.SelectedIndex = 0;
        UpdatePaperInfo();
    }

    private static bool IsA4(PaperSize p)
    {
        if (p.Kind == PaperKind.A4 || p.PaperName.Contains("A4", StringComparison.OrdinalIgnoreCase))
            return true;
        int w = Math.Min(p.Width, p.Height);
        int h = Math.Max(p.Width, p.Height);
        return Math.Abs(w - 827) <= 10 && Math.Abs(h - 1169) <= 10;
    }

    private void UpdatePaperInfo()
    {
        if (cmbPaper.SelectedItem is not PaperItem p)
        {
            lblPaperInfo.Text = string.Empty;
            return;
        }
        float w = p.Size.Width / 100f * 25.4f;
        float h = p.Size.Height / 100f * 25.4f;
        lblPaperInfo.Text = $"{w:0.#} × {h:0.#} mm";
    }

    private void OpenImage()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "图片文件|*.jpg;*.jpeg;*.png;*.bmp;*.gif|所有文件|*.*",
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
            LoadImage(dialog.FileName);
    }

    private void LoadImage(string path)
    {
        try
        {
            using var source = Image.FromFile(path);
            var copy = new Bitmap(source);
            currentImage?.Dispose();
            currentImage = copy;
            currentPath = path;
            lblFile.Text = Path.GetFileName(path);
            lblImageInfo.Text = $"{copy.Width} × {copy.Height} px   ·   {(copy.Width >= copy.Height ? "横图" : "竖图")}";
            preview.Invalidate();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "图片打开失败：" + ex.Message, "提示", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    private PrintDocument CreateDocument()
    {
        if (cmbPrinter.SelectedItem is not string printerName || printerName == "未检测到打印机")
            throw new InvalidOperationException("没有检测到可用打印机。请先在 Windows 中安装打印机。 ");

        var doc = new PrintDocument();
        doc.PrinterSettings.PrinterName = printerName;
        doc.PrinterSettings.Copies = (short)nudCopies.Value;
        doc.OriginAtMargins = false;

        if (cmbPaper.SelectedItem is PaperItem paper)
            doc.DefaultPageSettings.PaperSize = paper.Size;

        doc.DefaultPageSettings.Landscape = rbLandscape.Checked;
        ApplyQuality(doc);
        doc.PrintPage += PrintPage;
        return doc;
    }

    private void ApplyQuality(PrintDocument doc)
    {
        try
        {
            var resolutions = doc.PrinterSettings.PrinterResolutions.Cast<PrinterResolution>().ToList();
            if (resolutions.Count == 0) return;

            PrinterResolution selected;
            if (cmbQuality.SelectedIndex == 0)
            {
                selected = resolutions
                    .Where(r => r.X > 0 && r.Y > 0)
                    .OrderByDescending(r => r.X * r.Y)
                    .FirstOrDefault() ?? resolutions[0];
            }
            else
            {
                selected = resolutions.FirstOrDefault(r => r.Kind == PrinterResolutionKind.Medium)
                           ?? resolutions.FirstOrDefault(r => r.Kind == PrinterResolutionKind.Low)
                           ?? resolutions[0];
            }
            doc.DefaultPageSettings.PrinterResolution = selected;
        }
        catch { }
    }

    private void ShowPrinterProperties()
    {
        try
        {
            using var doc = CreateDocument();
            using var dialog = new PrintDialog
            {
                Document = doc,
                UseEXDialog = true,
                AllowPrintToFile = false,
                AllowSelection = false,
                AllowSomePages = false
            };
            dialog.ShowDialog(this);
            LoadPapers();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "打印机设置", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private void PrintNow()
    {
        if (currentImage == null)
        {
            MessageBox.Show(this, "请先添加一张需要打印的图片。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        try
        {
            using var doc = CreateDocument();
            doc.Print();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "打印失败：" + ex.Message, "错误", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void PrintPage(object? sender, PrintPageEventArgs e)
    {
        if (currentImage == null) return;

        e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        e.Graphics.SmoothingMode = SmoothingMode.HighQuality;

        var page = new RectangleF(0, 0, e.PageBounds.Width, e.PageBounds.Height);
        DrawImageToPage(e.Graphics, currentImage, page, cmbLayout.SelectedIndex == 0, cmbPosition.SelectedIndex);
        e.HasMorePages = false;
    }

    private PreviewState GetPreviewState()
    {
        float paperW = 210f, paperH = 297f;
        if (cmbPaper.SelectedItem is PaperItem p)
        {
            paperW = p.Size.Width / 100f * 25.4f;
            paperH = p.Size.Height / 100f * 25.4f;
        }
        if (rbLandscape.Checked)
            (paperW, paperH) = (paperH, paperW);

        return new PreviewState(
            currentImage,
            paperW,
            paperH,
            cmbLayout.SelectedIndex == 0,
            cmbPosition.SelectedIndex,
            chkShowSafeArea.Checked);
    }

    private static void DrawImageToPage(Graphics g, Image image, RectangleF page, bool cropFill, int positionIndex)
    {
        if (cropFill)
        {
            float sourceAspect = image.Width / (float)image.Height;
            float pageAspect = page.Width / page.Height;
            RectangleF src;

            if (sourceAspect > pageAspect)
            {
                float srcW = image.Height * pageAspect;
                float srcX = positionIndex == 2 ? 0 : (image.Width - srcW) / 2f;
                src = new RectangleF(srcX, 0, srcW, image.Height);
            }
            else
            {
                float srcH = image.Width / pageAspect;
                float srcY = positionIndex == 1 || positionIndex == 2 ? 0 : (image.Height - srcH) / 2f;
                src = new RectangleF(0, srcY, image.Width, srcH);
            }
            g.DrawImage(image, page, src, GraphicsUnit.Pixel);
            return;
        }

        float scale = Math.Min(page.Width / image.Width, page.Height / image.Height);
        float w = image.Width * scale;
        float h = image.Height * scale;
        float x = positionIndex == 2 ? 0 : (page.Width - w) / 2f;
        float y = positionIndex == 0 ? (page.Height - h) / 2f : 0;
        g.DrawImage(image, new RectangleF(x, y, w, h));
    }

    private sealed class PaperItem
    {
        public PaperSize Size { get; }
        public PaperItem(PaperSize size) => Size = size;
        public override string ToString() => Size.PaperName;
    }

    private sealed record PreviewState(
        Image? Image,
        float PaperWmm,
        float PaperHmm,
        bool CropFill,
        int PositionIndex,
        bool ShowSafeArea);

    private sealed class PrintPreviewPanel : Panel
    {
        public Func<PreviewState>? StateProvider { get; set; }

        public PrintPreviewPanel()
        {
            DoubleBuffered = true;
            ResizeRedraw = true;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            if (StateProvider == null) return;
            var s = StateProvider();

            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;

            const float margin = 28f;
            float scale = Math.Min(
                Math.Max(1, ClientSize.Width - margin * 2) / s.PaperWmm,
                Math.Max(1, ClientSize.Height - margin * 2) / s.PaperHmm);

            float paperW = s.PaperWmm * scale;
            float paperH = s.PaperHmm * scale;
            float px = (ClientSize.Width - paperW) / 2f;
            float py = (ClientSize.Height - paperH) / 2f;

            using var shadow = new SolidBrush(Color.FromArgb(28, 0, 0, 0));
            e.Graphics.FillRectangle(shadow, px + 7, py + 8, paperW, paperH);
            e.Graphics.FillRectangle(Brushes.White, px, py, paperW, paperH);

            if (s.Image != null)
            {
                var page = new RectangleF(px, py, paperW, paperH);
                DrawImageToPage(e.Graphics, s.Image, page, s.CropFill, s.PositionIndex);
            }
            else
            {
                using var titleFont = new Font("Microsoft YaHei UI", 12F, FontStyle.Bold);
                using var subFont = new Font("Microsoft YaHei UI", 9F);
                string title = "把图片拖到这里";
                string sub = "或者点击左上角“添加图片”";
                var ts = e.Graphics.MeasureString(title, titleFont);
                var ss = e.Graphics.MeasureString(sub, subFont);
                e.Graphics.DrawString(title, titleFont, Brushes.DimGray,
                    px + (paperW - ts.Width) / 2f, py + paperH / 2f - 28);
                e.Graphics.DrawString(sub, subFont, Brushes.Gray,
                    px + (paperW - ss.Width) / 2f, py + paperH / 2f + 4);
            }

            if (s.ShowSafeArea)
            {
                float inset = 5f * scale;
                using var pen = new Pen(Color.FromArgb(170, 255, 120, 0), 1) { DashStyle = DashStyle.Dash };
                e.Graphics.DrawRectangle(pen, px + inset, py + inset, paperW - inset * 2, paperH - inset * 2);
            }

            using var paperPen = new Pen(Color.FromArgb(215, 218, 224), 1);
            e.Graphics.DrawRectangle(paperPen, px, py, paperW, paperH);

            using var badgeFont = new Font("Microsoft YaHei UI", 8F);
            string badge = $"{s.PaperWmm:0.#} × {s.PaperHmm:0.#} mm";
            var badgeSize = e.Graphics.MeasureString(badge, badgeFont);
            var badgeRect = new RectangleF(px + 10, py + 10, badgeSize.Width + 14, badgeSize.Height + 6);
            using var badgeBg = new SolidBrush(Color.FromArgb(220, 255, 255, 255));
            e.Graphics.FillRectangle(badgeBg, badgeRect);
            e.Graphics.DrawString(badge, badgeFont, Brushes.DimGray, badgeRect.X + 7, badgeRect.Y + 3);
        }
    }
}
