(function () {
  "use strict";

  var app = window.external.Application;
  var BITMAP_SHAPE = 5, GROUP_SHAPE = 7;
  var TRACE_DETAILED_LOGO = 3, TRACE_BACKGROUND_AUTOMATIC = 1;
  var PLUGIN_CATEGORY = "ab489730-8791-45d2-a825-b78bbe0d6a5d";
  var COMMAND_OUTER = "SCM.AutoContour.Native64.Outer";
  var COMMAND_OUTER_HOLES = "SCM.AutoContour.Native64.OuterHoles";
  var REGISTRY = "HKCU\\Software\\SCM\\AutoContourV9\\";
  var BLUE = [0, 102, 220];
  var busy = false, shell = null, nativeControls = {}, internalJob = null;

  function byId(id) { return document.getElementById(id); }
  function number(value) { return Number(value || 0); }
  function errorText(error) {
    return error && (error.description || error.message) ?
      String(error.description || error.message) : String(error || "未知错误");
  }
  function selectedMode() {
    if (byId("modeInner").checked) { return "inner"; }
    if (byId("modeBoth").checked) { return "both"; }
    return "outer";
  }
  function modeName(mode) {
    if (mode === "inner") { return "产品内部寻边"; }
    if (mode === "both") { return "内外同时寻边"; }
    return "产品外形寻边";
  }
  function setStatus(text, kind) {
    var element = byId("status");
    element.className = "status" + (kind ? " " + kind : "");
    element.innerText = text;
  }
  function setProgress(percent, text, kind) {
    var value = Math.max(0, Math.min(100, Math.round(percent)));
    byId("progressBox").className = "progress-box " + (kind || "running");
    byId("progressText").innerText = text;
    byId("progressValue").innerText = value + "%";
    byId("progressBar").style.width = value + "%";
  }
  function setBusy(value) {
    busy = value;
    byId("startButton").disabled = value;
    byId("startButton").innerText = value ? "正在识别……" : "开始寻边";
  }
  function activeBitmap() {
    var range, shape;
    if (!app || !app.Documents || number(app.Documents.Count) === 0) { return null; }
    range = app.ActiveSelectionRange;
    if (!range || number(range.Count) !== 1) { return null; }
    shape = range.Item(1);
    return shape && number(shape.Type) === BITMAP_SHAPE ? shape : null;
  }
  function refreshSelection() {
    var card, title, info, source, range;
    if (busy) { return; }
    card = byId("selectionCard"); title = byId("selectionTitle"); info = byId("selectionInfo");
    try {
      source = activeBitmap();
      if (source) {
        card.className = "selection-card ready";
        title.innerText = "位图已就绪";
        info.innerText = source.Bitmap.SizeWidth + " × " + source.Bitmap.SizeHeight + " 像素";
        byId("startButton").disabled = false;
        return;
      }
      range = app && app.Documents && number(app.Documents.Count) ? app.ActiveSelectionRange : null;
      card.className = range && number(range.Count) === 1 ? "selection-card error" : "selection-card neutral";
      title.innerText = range && number(range.Count) === 1 ? "选中的对象不是位图" : "请只选择一张位图";
      info.innerText = range ? "当前选择数量：" + range.Count : "打开文档后选择 PNG、JPG 等位图对象";
      byId("startButton").disabled = true;
    } catch (error) {
      card.className = "selection-card error";
      title.innerText = "无法读取当前选择";
      info.innerText = errorText(error);
      byId("startButton").disabled = true;
    }
  }
  function updateModeUI() {
    var mode = selectedMode(), holes = byId("includeHoles"), suffix = "";
    byId("modeOuterLabel").className = "mode-option" + (mode === "outer" ? " selected" : "");
    byId("modeInnerLabel").className = "mode-option" + (mode === "inner" ? " selected" : "");
    byId("modeBothLabel").className = "mode-option" + (mode === "both" ? " selected" : "");
    if (mode === "inner") {
      holes.checked = false; holes.disabled = true; byId("holesRow").className = "holes-row disabled";
      byId("outputSummary").innerText = "蓝色内部图案路径";
    } else {
      holes.disabled = false; byId("holesRow").className = "holes-row";
      suffix = holes.checked ? "＋标准圆孔" : "";
      byId("outputSummary").innerText = mode === "both" ?
        "制造级外轮廓＋内部图案" + suffix : "单条制造级外轮廓" + suffix;
    }
    byId("currentTask").innerText = "当前：" + modeName(mode) + suffix;
  }

  function ensureShell() {
    if (!shell) { shell = new ActiveXObject("WScript.Shell"); }
    return shell;
  }
  function registryWrite(name, value) {
    ensureShell().RegWrite(REGISTRY + name, String(value), "REG_SZ");
  }
  function registryRead(name, fallback) {
    try { return String(ensureShell().RegRead(REGISTRY + name)); }
    catch (ignore) { return fallback; }
  }
  function writeNativeOptions() {
    var detail = number(byId("detail").value);
    var smoothing = number(byId("smoothing").value);
    registryWrite("SourceMode", "auto");
    registryWrite("ThresholdPercent", number(byId("sensitivity").value).toFixed(0));
    registryWrite("GaussianSigma", (0.15 + smoothing * 0.0095).toFixed(3));
    registryWrite("SimplifyMillimeters", (0.01 + (100 - detail) * 0.01).toFixed(3));
    registryWrite("MinimumAreaSquareMillimeters", (number(byId("minArea").value) / 1000).toFixed(3));
    registryWrite("OffsetMillimeters", "0.000");
    registryWrite("LastStatus", "pending");
    registryWrite("LastMessage", "正在启动 V9.1.0 原生引擎");
  }
  function nativeControl(command) {
    var controls, control;
    if (nativeControls[command]) { return nativeControls[command]; }
    controls = app.CommandBars.Item("Standard").Controls;
    control = controls.AddCustomButton(PLUGIN_CATEGORY, command, 1, true);
    if (!control || !control.ID) { throw new Error("V9.1.0 原生命令没有注册成功"); }
    nativeControls[command] = control;
    return control;
  }
  function invokeNative(command) {
    var control = nativeControl(command);
    app.FrameWork.Automation.InvokeItem(String(control.ID));
  }

  function visitLeafShapes(shape, callback) {
    var index;
    if (!shape) { return; }
    if (number(shape.Type) === GROUP_SHAPE) {
      for (index = 1; index <= number(shape.Shapes.Count); index += 1) {
        visitLeafShapes(shape.Shapes.Item(index), callback);
      }
      return;
    }
    callback(shape);
  }
  function visitRangeLeaves(range, callback) {
    var index;
    for (index = 1; range && index <= number(range.Count); index += 1) {
      visitLeafShapes(range.Item(index), callback);
    }
  }
  function ensureLayer(document, name) {
    var layers = document.ActivePage.Layers, index, layer;
    for (index = 1; index <= number(layers.Count); index += 1) {
      layer = layers.Item(index);
      if (layer && String(layer.Name) === name) { return layer; }
    }
    return document.ActivePage.CreateLayer(name);
  }
  function styleInternal(shape) {
    try { shape.Fill.ApplyNoFill(); } catch (ignoreFill) { }
    try {
      shape.Outline.Width = 0.01;
      shape.Outline.Color.RGBAssign(BLUE[0], BLUE[1], BLUE[2]);
      shape.Name = "SCM_INNER_TRACE";
    } catch (ignoreStyle) { }
  }
  function failInternal(error) {
    try {
      if (internalJob && internalJob.workBitmap) { internalJob.workBitmap.Delete(); }
      if (internalJob && internalJob.commandStarted) { internalJob.document.EndCommandGroup(); }
    } catch (ignoreCleanup) { }
    internalJob = null;
    setBusy(false);
    setProgress(100, "处理未完成", "failed");
    setStatus("内部寻边失败：" + errorText(error), "failed");
    refreshSelection();
  }
  function finishInternalTrace() {
    var result, layer;
    try {
      result = internalJob.settings.Finish();
      if (!result || number(result.Count) === 0) { throw new Error("没有识别到内部图案"); }
      internalJob.result = result;
      layer = ensureLayer(internalJob.document, "自动寻边-内部图案");
      visitRangeLeaves(result, styleInternal);
      try { result.MoveToLayer(layer); } catch (ignoreMove) { }
      try { internalJob.workBitmap.Delete(); } catch (ignoreDelete) { }
      internalJob.workBitmap = null;
      if (internalJob.commandStarted) { internalJob.document.EndCommandGroup(); internalJob.commandStarted = false; }
      try { internalJob.document.ClearSelection(); result.CreateSelection(); } catch (ignoreSelection) { }
      setBusy(false);
      setProgress(100, "寻边路径已生成", "success");
      setStatus(internalJob.mode === "both" ?
        "完成：已生成红色 V9 原生外轮廓和蓝色内部图案路径" :
        "完成：已生成蓝色内部图案路径", "success");
      internalJob = null;
      refreshSelection();
    } catch (error) { failInternal(error); }
  }
  function startInternalTrace(source, mode) {
    var settings;
    try {
      internalJob = { source: source, mode: mode, document: app.ActiveDocument,
        workBitmap: null, settings: null, result: null, commandStarted: false };
      internalJob.document.BeginCommandGroup("SCM V9.1.0 内部图案寻边");
      internalJob.commandStarted = true;
      setProgress(mode === "both" ? 78 : 18, "正在准备内部图案识别", "running");
      internalJob.workBitmap = source.Duplicate();
      settings = internalJob.workBitmap.Bitmap.Trace(TRACE_DETAILED_LOGO);
      settings.Smoothing = Math.max(0, Math.min(30, Math.round(number(byId("smoothing").value) * 0.3)));
      settings.CornerSmoothness = 6;
      settings.DetailLevelPercent = number(byId("detail").value);
      settings.BackgroundRemovalMode = TRACE_BACKGROUND_AUTOMATIC;
      settings.RemoveBackground = true;
      settings.RemoveEntireBackColor = false;
      settings.RemoveOverlap = true;
      settings.MergeAdjacentObjects = true;
      settings.GroupObjectsByColor = false;
      settings.DeleteOriginalObject = false;
      try { settings.SetColorCount(number(byId("colors").value)); } catch (ignoreColors) { }
      internalJob.settings = settings;
      setProgress(mode === "both" ? 88 : 62, "正在提取内部图案细节", "running");
      window.setTimeout(finishInternalTrace, 80);
    } catch (error) { failInternal(error); }
  }

  function finishOuter(source, mode) {
    var status = registryRead("LastStatus", "failed");
    var message = registryRead("LastMessage", "原生引擎没有返回执行结果");
    if (status !== "success") {
      setBusy(false);
      setProgress(100, "处理未完成", "failed");
      setStatus("寻边失败：" + message, "failed");
      refreshSelection();
      return;
    }
    if (mode === "both") {
      setStatus("外轮廓完成，正在生成内部图案……", "running");
      window.setTimeout(function () { startInternalTrace(source, mode); }, 60);
      return;
    }
    setBusy(false);
    setProgress(100, "寻边路径已生成", "success");
    setStatus("完成：" + message, "success");
    refreshSelection();
  }
  function startTrace() {
    var source, mode, command;
    if (busy) { return; }
    source = activeBitmap();
    if (!source) { setStatus("请先只选择一张位图", "failed"); refreshSelection(); return; }
    mode = selectedMode();
    setBusy(true);
    setStatus("正在执行“" + modeName(mode) + "”，请稍候……", "running");
    setProgress(3, "任务已开始", "running");
    if (mode === "inner") {
      window.setTimeout(function () { startInternalTrace(source, mode); }, 60);
      return;
    }
    try {
      writeNativeOptions();
      command = byId("includeHoles").checked ? COMMAND_OUTER_HOLES : COMMAND_OUTER;
      setProgress(15, "V9 原生引擎正在规划外轮廓", "running");
      invokeNative(command);
      setProgress(76, "原生外轮廓处理完成，正在读取复检结果", "running");
      finishOuter(source, mode);
    } catch (error) {
      setBusy(false);
      setProgress(100, "处理未完成", "failed");
      setStatus("启动失败：" + errorText(error) + "。请确认安装的是 V9.1.0，而不是 V9.0.7。", "failed");
      refreshSelection();
    }
  }

  function updateSliderLabels() {
    byId("detailValue").innerText = byId("detail").value;
    byId("smoothingValue").innerText = byId("smoothing").value;
    byId("sensitivityValue").innerText = byId("sensitivity").value;
    byId("colorsValue").innerText = byId("colors").value;
    byId("minAreaValue").innerText = (number(byId("minArea").value) / 1000).toFixed(3) + " mm²";
  }
  function resetRecommended() {
    byId("detail").value = 98; byId("smoothing").value = 42;
    byId("sensitivity").value = 50; byId("colors").value = 56;
    byId("minArea").value = 10; updateSliderLabels();
  }
  function toggleAdvanced() {
    var panel = byId("advancedPanel"), hidden = panel.className.indexOf("hidden") >= 0;
    panel.className = hidden ? "advanced-panel" : "advanced-panel hidden";
    byId("advancedArrow").innerText = hidden ? "▲" : "▼";
  }
  function initialize() {
    byId("startButton").onclick = startTrace;
    byId("advancedToggle").onclick = toggleAdvanced;
    byId("resetButton").onclick = resetRecommended;
    byId("modeOuter").onchange = updateModeUI;
    byId("modeInner").onchange = updateModeUI;
    byId("modeBoth").onchange = updateModeUI;
    byId("includeHoles").onchange = updateModeUI;
    byId("detail").onchange = updateSliderLabels; byId("detail").oninput = updateSliderLabels;
    byId("smoothing").onchange = updateSliderLabels; byId("smoothing").oninput = updateSliderLabels;
    byId("sensitivity").onchange = updateSliderLabels; byId("sensitivity").oninput = updateSliderLabels;
    byId("colors").onchange = updateSliderLabels; byId("colors").oninput = updateSliderLabels;
    byId("minArea").onchange = updateSliderLabels; byId("minArea").oninput = updateSliderLabels;
    try { window.external.RegisterEventListener("SelectionChange", "AutoContourV9SelectionChanged()"); }
    catch (ignoreEvent) { }
    updateSliderLabels(); updateModeUI(); setProgress(0, "等待开始", "idle"); refreshSelection();
  }

  window.AutoContourV9SelectionChanged = refreshSelection;
  window.onunload = function () {
    try { window.external.UnregisterEventListener("SelectionChange"); } catch (ignoreEvent) { }
  };
  if (document.readyState === "loading") { document.addEventListener("DOMContentLoaded", initialize); }
  else { initialize(); }
}());
