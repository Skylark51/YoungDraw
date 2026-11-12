using System;
using System.Diagnostics;
using System.IO;
using System.IO.Packaging;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using System.Xml.Linq;
using Microsoft.Win32;

[assembly: AssemblyVersion("1.0.*")]

namespace YoungDraw.PowerPoint
{
    [ComImport, Guid("B65AD801-ABAF-11D0-BB8B-00A0C90F2744"), InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IOfficeAddIn
    {
        [DispId(1)] void OnConnection([MarshalAs(UnmanagedType.IDispatch)] object application, int mode,
            [MarshalAs(UnmanagedType.IDispatch)] object addIn, ref Array custom);
        [DispId(2)] void OnDisconnection(int mode, ref Array custom);
        [DispId(3)] void OnAddInsUpdate(ref Array custom);
        [DispId(4)] void OnStartupComplete(ref Array custom);
        [DispId(5)] void OnBeginShutdown(ref Array custom);
    }

    [ComImport, Guid("000C0396-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IRibbonExtensibility
    {
        [DispId(1)] [return: MarshalAs(UnmanagedType.BStr)]
        string GetCustomUI([MarshalAs(UnmanagedType.BStr)] string ribbonId);
    }

    [ComVisible(true), Guid("D2483040-86AF-4FC1-A6EC-B808483787EB")]
    [ProgId("YoungDraw.PowerPointAddIn"), ClassInterface(ClassInterfaceType.AutoDispatch)]
    public sealed class AddIn : IOfficeAddIn, IRibbonExtensibility
    {
        private const string ButtonId = "YoungDrawOpenChemDraw";
        private const string RegistryPath = @"Software\Microsoft\Office\PowerPoint\Addins\YoungDraw.PowerPointAddIn";
        private const int EmbeddedOleObject = 7;
        private const int ShapeSelection = 2;
        private const int OpenXmlPresentation = 24;
        private const int OpenXmlPresentationWithMacros = 25;
        private const int SelectionChanged = 2001;
        private static readonly Guid PowerPointEvents = new Guid("914934C2-5A91-11CF-8700-00AA0060263B");
        private static readonly XNamespace Presentation = "http://schemas.openxmlformats.org/presentationml/2006/main";
        private static readonly XNamespace Relationships = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
        private object application;
        private object ribbon;
        private Action<object> selectionChanged;

        public void OnConnection(object app, int mode, object addIn, ref Array custom)
        {
            application = app;
            selectionChanged = SelectionChangedInPowerPoint;
            ComEventsHelper.Combine(application, PowerPointEvents, SelectionChanged, selectionChanged);
        }

        public void OnDisconnection(int mode, ref Array custom)
        {
            if (application != null && selectionChanged != null)
                ComEventsHelper.Remove(application, PowerPointEvents, SelectionChanged, selectionChanged);
            selectionChanged = null;
            ribbon = null;
            application = null;
        }

        public void OnAddInsUpdate(ref Array custom) { }
        public void OnStartupComplete(ref Array custom) { }
        public void OnBeginShutdown(ref Array custom) { }

        public string GetCustomUI(string ribbonId)
        {
            return @"<customUI xmlns=""http://schemas.microsoft.com/office/2009/07/customui"" onLoad=""RibbonLoaded"">
  <contextMenus>
    <contextMenu idMso=""ContextMenuGraphicOleClassic"">
      <button id=""" + ButtonId + @""" label=""YoungDraw로 열기"" onAction=""OpenInYoungDraw"" getVisible=""CanOpenInYoungDraw""/>
    </contextMenu>
  </contextMenus>
</customUI>";
        }

        public void RibbonLoaded(object ribbonUi) { ribbon = ribbonUi; }

        private void SelectionChangedInPowerPoint(object selection)
        {
            if (ribbon != null)
                ((dynamic)ribbon).InvalidateControl(ButtonId);
        }

        public bool CanOpenInYoungDraw(object control)
        {
            try
            {
                return application != null && SelectedChemDraw(((dynamic)application).ActiveWindow) != null;
            }
            catch (COMException) { return false; }
        }

        private static object SelectedChemDraw(dynamic window)
        {
            if (window == null) return null;
            dynamic selection = window.Selection;
            if ((int)selection.Type != ShapeSelection || (int)selection.ShapeRange.Count != 1) return null;
            dynamic shape = selection.ShapeRange.Item(1);
            if ((int)shape.Type != EmbeddedOleObject) return null;
            return IsChemDraw((string)shape.OLEFormat.ProgID) ? (object)shape : null;
        }

        private static bool IsChemDraw(string progId)
        {
            if (progId == null) return false;
            foreach (string family in new[] { "ChemDraw.Document", "ChemDraw_x64.Document" })
                if (progId.Equals(family, StringComparison.OrdinalIgnoreCase) ||
                    progId.StartsWith(family + ".", StringComparison.OrdinalIgnoreCase))
                    return true;
            return false;
        }

        public void OpenInYoungDraw(object control)
        {
            try
            {
                dynamic window = ((dynamic)application).ActiveWindow;
                dynamic shape = SelectedChemDraw(window);
                if (shape == null) return;
                OpenCopy(window.Presentation, (int)window.View.Slide.SlideID, (int)shape.Id);
            }
            catch (Exception error)
            {
                MessageBox.Show("ChemDraw 개체를 YoungDraw에서 열 수 없습니다.\n\n" + error.Message,
                    "PowerPoint · YoungDraw", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        private static void OpenCopy(dynamic presentation, int slideId, int shapeId)
        {
            string executable;
            using (var key = Registry.CurrentUser.OpenSubKey(RegistryPath))
                executable = key == null ? null : key.GetValue("YoungDrawPath") as string;
            if (!File.Exists(executable))
                throw new FileNotFoundException("YoungDraw 실행 파일을 찾을 수 없습니다. PowerPoint 연동을 다시 설치하세요.");

            bool hasMacros = Convert.ToBoolean((object)presentation.HasVBProject);
            string snapshot = Path.Combine(Path.GetTempPath(), "YoungDraw-" + Guid.NewGuid().ToString("N") +
                (hasMacros ? ".pptm" : ".pptx"));
            try
            {
                // SaveCopyAs captures unsaved slide edits without changing the open presentation.
                presentation.SaveCopyAs(snapshot, hasMacros ? OpenXmlPresentationWithMacros : OpenXmlPresentation, 0);
                using (var package = Package.Open(snapshot, FileMode.Open, FileAccess.Read))
                {
                    var part = FindEmbedding(package, slideId, shapeId);
                    using (var source = part.GetStream(FileMode.Open, FileAccess.Read))
                    {
                        if (source.Length > 64 * 1024 * 1024)
                            throw new InvalidDataException("ChemDraw 개체는 64 MB까지 열 수 있습니다.");
                        var start = new ProcessStartInfo(executable, "--open-powerpoint-chemdraw")
                        {
                            UseShellExecute = false,
                            RedirectStandardInput = true
                        };
                        using (var process = Process.Start(start))
                        using (var pipe = process.StandardInput.BaseStream)
                            source.CopyTo(pipe);
                    }
                }
            }
            finally
            {
                File.Delete(snapshot);
            }
        }

        private static PackagePart FindEmbedding(Package package, int slideId, int shapeId)
        {
            var presentation = package.GetPart(new Uri("/ppt/presentation.xml", UriKind.Relative));
            var slide = ReadXml(presentation).Descendants(Presentation + "sldId")
                .FirstOrDefault(item => (int?)item.Attribute("id") == slideId);
            if (slide == null) throw new InvalidDataException("선택한 슬라이드를 찾을 수 없습니다.");
            var slidePart = RelatedPart(package, presentation, (string)slide.Attribute(Relationships + "id"), "slide");
            var frame = ReadXml(slidePart).Descendants(Presentation + "graphicFrame")
                .FirstOrDefault(item => (int?)item.Element(Presentation + "nvGraphicFramePr")
                    .Element(Presentation + "cNvPr").Attribute("id") == shapeId);
            var embedding = frame == null ? null : frame.Descendants(Presentation + "oleObj")
                .FirstOrDefault(item => item.Element(Presentation + "embed") != null && IsChemDraw((string)item.Attribute("progId")));
            if (embedding == null) throw new InvalidDataException("선택한 ChemDraw 개체의 원본을 찾을 수 없습니다.");
            return RelatedPart(package, slidePart, (string)embedding.Attribute(Relationships + "id"), "oleObject");
        }

        private static XDocument ReadXml(PackagePart part)
        {
            using (var source = part.GetStream(FileMode.Open, FileAccess.Read))
                return XDocument.Load(source);
        }

        private static PackagePart RelatedPart(Package package, PackagePart part, string id, string kind)
        {
            var relation = part.GetRelationship(id);
            if (relation.TargetMode != TargetMode.Internal || relation.RelationshipType != Relationships.NamespaceName + "/" + kind)
                throw new InvalidDataException("PowerPoint 개체의 원본 경로를 읽을 수 없습니다.");
            return package.GetPart(PackUriHelper.ResolvePartUri(part.Uri, relation.TargetUri));
        }
    }
}
