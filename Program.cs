using System.Reflection;

namespace CalendarPrintAssistant;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();
        var form = new MainForm();
        typeof(MainForm)
            .GetMethod("LoadPapers", BindingFlags.Instance | BindingFlags.NonPublic)?
            .Invoke(form, null);
        Application.Run(form);
    }
}
