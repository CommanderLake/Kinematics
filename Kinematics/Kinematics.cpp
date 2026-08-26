// Kinematics.cpp : interactive 2D linkage editor and constraint simulator.
#include "framework.h"
#include "Kinematics.h"

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

namespace {

constexpr int kToolbarHeight = 58;
constexpr int kSidebarWidth = 310;
constexpr int kStatusHeight = 28;
constexpr double kGridMillimetres = 10.0;
constexpr double kPi = 3.14159265358979323846;

constexpr int IDC_SNAP = 2001;
constexpr int IDC_POINT_FIXED = 2002;
constexpr int IDC_POINT_X = 2003;
constexpr int IDC_POINT_Y = 2004;
constexpr int IDC_APPLY_POINT = 2005;
constexpr int IDC_LINK_FIXED = 2006;
constexpr int IDC_LINK_LENGTH = 2007;
constexpr int IDC_APPLY_LINK = 2008;
constexpr int IDC_DELETE_SELECTED = 2009;
constexpr int IDC_CLEAR_MODEL = 2010;
constexpr int IDC_RESET_VIEW_BUTTON = 2011;
constexpr int IDC_SELECTION_SUMMARY = 2012;
constexpr int IDC_LINK_MIN = 2013;
constexpr int IDC_LINK_MAX = 2014;
constexpr int IDC_APPLY_ANGLE = 2015;
constexpr int IDC_ANGLE_MIN = 2016;
constexpr int IDC_ANGLE_MAX = 2017;

// Dark theme palette.
constexpr COLORREF kWindow = RGB(18, 22, 28);
constexpr COLORREF kPanel = RGB(27, 33, 42);
constexpr COLORREF kPanelRaised = RGB(36, 44, 55);
constexpr COLORREF kToolbar = RGB(13, 17, 23);
constexpr COLORREF kBorder = RGB(57, 68, 82);
constexpr COLORREF kText = RGB(226, 232, 239);
constexpr COLORREF kMutedText = RGB(142, 155, 170);
constexpr COLORREF kBlue = RGB(49, 172, 226);
constexpr COLORREF kOrange = RGB(237, 145, 70);
constexpr COLORREF kPurple = RGB(188, 119, 234);

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
Vec2 operator*(const Vec2& a, double value) { return { a.x * value, a.y * value }; }

double Length(const Vec2& value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

struct ModelPoint {
    int id = 0;
    Vec2 position;
    bool fixed = false;
};

struct ModelLink {
    int id = 0;
    int pointA = 0;
    int pointB = 0;
    bool fixedLength = true;
    double targetLength = 0.0;
    // Zero means that side of the travel range is unrestricted.
    double minLength = 0.0;
    double maxLength = 0.0;
};

struct AngleMeasurement {
    int id = 0;
    int pointA = 0;
    int vertex = 0;
    int pointC = 0;
    bool hasMinLimit = false;
    double minDegrees = 0.0;
    bool hasMaxLimit = false;
    double maxDegrees = 180.0;
};

enum class Tool { Select, Point, Link, Angle };
enum class SelectionKind { None, Point, Link, Angle };

struct Selection {
    SelectionKind kind = SelectionKind::None;
    int id = 0;
};

struct Controls {
    HWND summary = nullptr;
    HWND snap = nullptr;
    HWND pointFixed = nullptr;
    HWND pointXLabel = nullptr;
    HWND pointX = nullptr;
    HWND pointYLabel = nullptr;
    HWND pointY = nullptr;
    HWND applyPoint = nullptr;
    HWND linkFixed = nullptr;
    HWND linkLengthLabel = nullptr;
    HWND linkLength = nullptr;
    HWND linkMinLabel = nullptr;
    HWND linkMin = nullptr;
    HWND linkMaxLabel = nullptr;
    HWND linkMax = nullptr;
    HWND applyLink = nullptr;
    HWND angleMinLabel = nullptr;
    HWND angleMin = nullptr;
    HWND angleMaxLabel = nullptr;
    HWND angleMax = nullptr;
    HWND applyAngle = nullptr;
    HWND deleteSelected = nullptr;
    HWND clearModel = nullptr;
    HWND resetView = nullptr;
};

std::vector<ModelPoint> g_points;
std::vector<ModelLink> g_links;
std::vector<AngleMeasurement> g_angles;
int g_nextPointId = 1;
int g_nextLinkId = 1;
int g_nextAngleId = 1;

Tool g_tool = Tool::Select;
Selection g_selection;
int g_linkStartPointId = 0;
std::vector<int> g_anglePointIds;

bool g_snapToGrid = true;
bool g_defaultPointFixed = false;
bool g_defaultLinkFixed = true;
bool g_draggingPoint = false;
bool g_panning = false;
int g_draggedPointId = 0;
POINT g_lastMouse = {};
POINT g_panMouseStart = {};
double g_panStartX = 0.0;
double g_panStartY = 0.0;
double g_zoom = 2.0; // pixels per millimetre
double g_panX = 0.0;
double g_panY = 0.0;

Controls g_controls;
HFONT g_uiFont = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_smallFont = nullptr;
HBRUSH g_panelBrush = nullptr;
HBRUSH g_editBrush = nullptr;
bool g_syncingControls = false;
std::wstring g_currentFilePath;
std::wstring g_startupFilePath;
bool g_dirty = false;
RECT g_labelArea = {};
std::vector<RECT> g_labelBounds;
std::vector<std::pair<int, RECT>> g_angleHitBounds;

RECT CanvasRect(HWND hWnd) {
    RECT client = {};
    GetClientRect(hWnd, &client);
    return { 0, kToolbarHeight, (std::max)(0L, client.right - kSidebarWidth),
             (std::max)(static_cast<LONG>(kToolbarHeight), client.bottom - kStatusHeight) };
}

void InvalidateCanvasAndStatus(HWND hWnd) {
    const RECT canvas = CanvasRect(hWnd);
    InvalidateRect(hWnd, &canvas, FALSE);
    RECT client = {};
    GetClientRect(hWnd, &client);
    const RECT status = { 0, client.bottom - kStatusHeight, client.right, client.bottom };
    InvalidateRect(hWnd, &status, FALSE);
}

bool Contains(const RECT& rect, POINT point) {
    return point.x >= rect.left && point.x <= rect.right &&
           point.y >= rect.top && point.y <= rect.bottom;
}

POINT WorldToScreen(HWND hWnd, const Vec2& world) {
    const RECT canvas = CanvasRect(hWnd);
    const double centreX = (canvas.left + canvas.right) * 0.5;
    const double centreY = (canvas.top + canvas.bottom) * 0.5;
    return {
        static_cast<LONG>(std::lround(centreX + g_panX + world.x * g_zoom)),
        static_cast<LONG>(std::lround(centreY + g_panY - world.y * g_zoom))
    };
}

Vec2 ScreenToWorld(HWND hWnd, POINT screen) {
    const RECT canvas = CanvasRect(hWnd);
    const double centreX = (canvas.left + canvas.right) * 0.5;
    const double centreY = (canvas.top + canvas.bottom) * 0.5;
    return {
        (screen.x - centreX - g_panX) / g_zoom,
        -(screen.y - centreY - g_panY) / g_zoom
    };
}

Vec2 SnapPosition(Vec2 position) {
    if (g_snapToGrid) {
        position.x = std::round(position.x / kGridMillimetres) * kGridMillimetres;
        position.y = std::round(position.y / kGridMillimetres) * kGridMillimetres;
    }
    return position;
}

ModelPoint* FindPoint(int id) {
    for (auto& point : g_points) if (point.id == id) return &point;
    return nullptr;
}

const ModelPoint* FindPointConst(int id) {
    for (const auto& point : g_points) if (point.id == id) return &point;
    return nullptr;
}

ModelLink* FindLink(int id) {
    for (auto& link : g_links) if (link.id == id) return &link;
    return nullptr;
}

const AngleMeasurement* FindAngleConst(int id) {
    for (const auto& angle : g_angles) if (angle.id == id) return &angle;
    return nullptr;
}

AngleMeasurement* FindAngle(int id) {
    for (auto& angle : g_angles) if (angle.id == id) return &angle;
    return nullptr;
}

double CurrentLinkLength(const ModelLink& link) {
    const ModelPoint* a = FindPointConst(link.pointA);
    const ModelPoint* b = FindPointConst(link.pointB);
    return (a && b) ? Length(b->position - a->position) : 0.0;
}

double AngleDegrees(const AngleMeasurement& angle) {
    const ModelPoint* a = FindPointConst(angle.pointA);
    const ModelPoint* b = FindPointConst(angle.vertex);
    const ModelPoint* c = FindPointConst(angle.pointC);
    if (!a || !b || !c) return 0.0;
    const Vec2 first = a->position - b->position;
    const Vec2 second = c->position - b->position;
    const double denominator = Length(first) * Length(second);
    if (denominator < 1e-9) return 0.0;
    double cosine = (first.x * second.x + first.y * second.y) / denominator;
    cosine = (std::max)(-1.0, (std::min)(1.0, cosine));
    return std::acos(cosine) * 180.0 / kPi;
}

std::wstring FormatNumber(double value, int precision = 1) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%.*f", precision, value);
    return buffer;
}

std::wstring PointName(int id) { return L"P" + std::to_wstring(id); }

void SetText(HWND window, const std::wstring& text) {
    SetWindowTextW(window, text.c_str());
}

bool ReadDouble(HWND edit, double& result) {
    wchar_t buffer[128] = {};
    GetWindowTextW(edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    wchar_t* end = nullptr;
    const double value = std::wcstod(buffer, &end);
    while (end && *end == L' ') ++end;
    if (end == buffer || (end && *end != L'\0') || !std::isfinite(value)) return false;
    result = value;
    return true;
}

bool ReadOptionalDouble(HWND edit, bool& present, double& result) {
    wchar_t buffer[128] = {};
    GetWindowTextW(edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    wchar_t* text = buffer;
    while (*text == L' ' || *text == L'\t') ++text;
    if (*text == L'\0') {
        present = false;
        result = 0.0;
        return true;
    }
    wchar_t* end = nullptr;
    const double value = std::wcstod(text, &end);
    while (end && (*end == L' ' || *end == L'\t')) ++end;
    if (end == text || (end && *end != L'\0') || !std::isfinite(value)) return false;
    present = true;
    result = value;
    return true;
}

void ClearPendingSteps() {
    g_linkStartPointId = 0;
    g_anglePointIds.clear();
}

void SetTool(HWND hWnd, Tool tool) {
    g_tool = tool;
    ClearPendingSteps();
    InvalidateRect(hWnd, nullptr, FALSE);
}

void SolveConstraints(int controlledPointId, Vec2 requestedPosition) {
    ModelPoint* controlled = FindPoint(controlledPointId);
    if (!controlled) return;

    // The cursor is an initial pose request, not an unbreakable constraint.
    // Reapplying an unreachable cursor target every pass makes the final result
    // depend on link order and leaves earlier links stretched in closed loops.
    controlled->position = requestedPosition;

    auto projectLink = [&](const ModelLink& link) {
        ModelPoint* a = FindPoint(link.pointA);
        ModelPoint* b = FindPoint(link.pointB);
        if (!a || !b) return 0.0;

        Vec2 delta = b->position - a->position;
        double distance = Length(delta);
        if (distance < 1e-12) {
            delta = { 1.0, 0.0 };
            distance = 1.0;
        }
        double constrainedLength = distance;
        if (link.fixedLength && link.targetLength > 0.0) {
            constrainedLength = link.targetLength;
        } else if (!link.fixedLength && link.minLength > 0.0 && distance < link.minLength) {
            constrainedLength = link.minLength;
        } else if (!link.fixedLength && link.maxLength > 0.0 && distance > link.maxLength) {
            constrainedLength = link.maxLength;
        } else {
            return 0.0;
        }
        const double error = std::abs(distance - constrainedLength);
        // A dragged point has lower inverse mass, so the rest of the mechanism
        // follows it where possible, but it can still move to satisfy geometry.
        const double weightA = a->fixed ? 0.0 : (a->id == controlledPointId ? 0.05 : 1.0);
        const double weightB = b->fixed ? 0.0 : (b->id == controlledPointId ? 0.05 : 1.0);
        const double totalWeight = weightA + weightB;
        // A fixed-to-fixed frame member cannot be adjusted and should not keep
        // an otherwise converged solve running because of file-rounding noise.
        if (totalWeight <= 0.0) return 0.0;

        const Vec2 correction = delta * ((distance - constrainedLength) / distance);
        a->position = a->position + correction * (weightA / totalWeight);
        b->position = b->position - correction * (weightB / totalWeight);
        return error;
    };

    auto projectAngle = [&](const AngleMeasurement& angle) {
        if (!angle.hasMinLimit && !angle.hasMaxLimit) return 0.0;
        ModelPoint* a = FindPoint(angle.pointA);
        ModelPoint* b = FindPoint(angle.vertex);
        ModelPoint* c = FindPoint(angle.pointC);
        if (!a || !b || !c) return 0.0;

        const Vec2 first = a->position - b->position;
        const Vec2 second = c->position - b->position;
        const double firstSquared = first.x * first.x + first.y * first.y;
        const double secondSquared = second.x * second.x + second.y * second.y;
        if (firstSquared < 1e-12 || secondSquared < 1e-12) return 0.0;

        const double signedAngle = std::atan2(first.x * second.y - first.y * second.x,
                                               first.x * second.x + first.y * second.y);
        const double currentAngle = std::abs(signedAngle);
        double constrainedAngle = currentAngle;
        if (angle.hasMinLimit && currentAngle < angle.minDegrees * kPi / 180.0)
            constrainedAngle = angle.minDegrees * kPi / 180.0;
        else if (angle.hasMaxLimit && currentAngle > angle.maxDegrees * kPi / 180.0)
            constrainedAngle = angle.maxDegrees * kPi / 180.0;
        else
            return 0.0;

        const double violation = currentAngle - constrainedAngle;
        const double orientation = signedAngle < 0.0 ? -1.0 : 1.0;
        Vec2 gradientA = { first.y / firstSquared, -first.x / firstSquared };
        Vec2 gradientC = { -second.y / secondSquared, second.x / secondSquared };
        gradientA = gradientA * orientation;
        gradientC = gradientC * orientation;
        const Vec2 gradientB = (gradientA + gradientC) * -1.0;

        const double weightA = a->fixed ? 0.0 : (a->id == controlledPointId ? 0.05 : 1.0);
        const double weightB = b->fixed ? 0.0 : (b->id == controlledPointId ? 0.05 : 1.0);
        const double weightC = c->fixed ? 0.0 : (c->id == controlledPointId ? 0.05 : 1.0);
        const double denominator =
            weightA * (gradientA.x * gradientA.x + gradientA.y * gradientA.y) +
            weightB * (gradientB.x * gradientB.x + gradientB.y * gradientB.y) +
            weightC * (gradientC.x * gradientC.x + gradientC.y * gradientC.y);
        if (denominator < 1e-15) return 0.0;

        const double scale = -violation / denominator;
        a->position = a->position + gradientA * (weightA * scale);
        b->position = b->position + gradientB * (weightB * scale);
        c->position = c->position + gradientC * (weightC * scale);
        return std::abs(violation);
    };

    // Alternating traversal removes the last-link bias of Gauss-Seidel while
    // retaining its quick convergence for the small linkage graphs used here.
    for (int iteration = 0; iteration < 5000; ++iteration) {
        double maximumLinkError = 0.0;
        double maximumAngleError = 0.0;
        if ((iteration & 1) == 0) {
            for (const ModelLink& link : g_links)
                maximumLinkError = (std::max)(maximumLinkError, projectLink(link));
            for (const AngleMeasurement& angle : g_angles)
                maximumAngleError = (std::max)(maximumAngleError, projectAngle(angle));
        } else {
            for (auto link = g_links.rbegin(); link != g_links.rend(); ++link)
                maximumLinkError = (std::max)(maximumLinkError, projectLink(*link));
            for (auto angle = g_angles.rbegin(); angle != g_angles.rend(); ++angle)
                maximumAngleError = (std::max)(maximumAngleError, projectAngle(*angle));
        }
        if (maximumLinkError < 1e-9 && maximumAngleError < 1e-11) break;
    }
}

int HitTestPoint(HWND hWnd, POINT mouse, double radius = 11.0) {
    int result = 0;
    double closest = radius;
    for (const ModelPoint& point : g_points) {
        const POINT screen = WorldToScreen(hWnd, point.position);
        const double distance = std::hypot(static_cast<double>(mouse.x - screen.x),
                                           static_cast<double>(mouse.y - screen.y));
        if (distance <= closest) {
            closest = distance;
            result = point.id;
        }
    }
    return result;
}

double DistanceToSegment(POINT point, POINT start, POINT end) {
    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 1e-9) {
        return std::hypot(static_cast<double>(point.x - start.x), static_cast<double>(point.y - start.y));
    }
    double t = ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared;
    t = (std::max)(0.0, (std::min)(1.0, t));
    return std::hypot(point.x - (start.x + t * dx), point.y - (start.y + t * dy));
}

int HitTestLink(HWND hWnd, POINT mouse) {
    int result = 0;
    double closest = 8.0;
    for (const ModelLink& link : g_links) {
        const ModelPoint* a = FindPointConst(link.pointA);
        const ModelPoint* b = FindPointConst(link.pointB);
        if (!a || !b) continue;
        const double distance = DistanceToSegment(mouse, WorldToScreen(hWnd, a->position),
                                                   WorldToScreen(hWnd, b->position));
        if (distance <= closest) {
            closest = distance;
            result = link.id;
        }
    }
    return result;
}

POINT AngleLabelPosition(HWND hWnd, const AngleMeasurement& angle) {
    const ModelPoint* a = FindPointConst(angle.pointA);
    const ModelPoint* b = FindPointConst(angle.vertex);
    const ModelPoint* c = FindPointConst(angle.pointC);
    if (!a || !b || !c) return {};
    double start = std::atan2(a->position.y - b->position.y, a->position.x - b->position.x);
    double finish = std::atan2(c->position.y - b->position.y, c->position.x - b->position.x);
    double sweep = finish - start;
    while (sweep > kPi) sweep -= 2.0 * kPi;
    while (sweep < -kPi) sweep += 2.0 * kPi;
    const double middle = start + sweep * 0.5;
    const Vec2 label = b->position + Vec2{ std::cos(middle), std::sin(middle) } * (36.0 / g_zoom);
    return WorldToScreen(hWnd, label);
}

int HitTestAngle(HWND hWnd, POINT mouse) {
    for (auto label = g_angleHitBounds.rbegin(); label != g_angleHitBounds.rend(); ++label) {
        if (Contains(label->second, mouse)) return label->first;
    }
    for (auto iterator = g_angles.rbegin(); iterator != g_angles.rend(); ++iterator) {
        const POINT label = AngleLabelPosition(hWnd, *iterator);
        if (std::abs(mouse.x - label.x) <= 30 && std::abs(mouse.y - label.y) <= 14) return iterator->id;
    }
    return 0;
}

void SyncProperties(HWND hWnd);

std::wstring FileNameFromPath(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

void UpdateWindowTitle(HWND hWnd) {
    const std::wstring name = g_currentFilePath.empty() ? L"Untitled" : FileNameFromPath(g_currentFilePath);
    const std::wstring title = L"Kinematics Studio - " + name + (g_dirty ? L" *" : L"");
    SetWindowTextW(hWnd, title.c_str());
}

void MarkDirty(HWND hWnd) {
    if (!g_dirty) {
        g_dirty = true;
        UpdateWindowTitle(hWnd);
    }
}

bool SaveModelToFile(HWND hWnd, const std::wstring& path) {
    std::ofstream output(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(hWnd, L"The model file could not be created.", L"Save model", MB_OK | MB_ICONERROR);
        return false;
    }

    output << std::setprecision(17);
    output << "KINEMATICS_MODEL 2\n";
    output << "VIEW " << g_zoom << ' ' << g_panX << ' ' << g_panY << "\n";
    output << "SETTINGS " << (g_snapToGrid ? 1 : 0) << ' '
           << (g_defaultPointFixed ? 1 : 0) << ' ' << (g_defaultLinkFixed ? 1 : 0) << "\n";
    output << "POINTS " << g_points.size() << "\n";
    for (const ModelPoint& point : g_points) {
        output << "POINT " << point.id << ' ' << point.position.x << ' ' << point.position.y
               << ' ' << (point.fixed ? 1 : 0) << "\n";
    }
    output << "LINKS " << g_links.size() << "\n";
    for (const ModelLink& link : g_links) {
        output << "LINK " << link.id << ' ' << link.pointA << ' ' << link.pointB << ' '
               << (link.fixedLength ? 1 : 0) << ' ' << link.targetLength << ' '
               << link.minLength << ' ' << link.maxLength << "\n";
    }
    output << "ANGLES " << g_angles.size() << "\n";
    for (const AngleMeasurement& angle : g_angles) {
        output << "ANGLE " << angle.id << ' ' << angle.pointA << ' ' << angle.vertex << ' '
               << angle.pointC << ' ' << (angle.hasMinLimit ? 1 : 0) << ' ' << angle.minDegrees
               << ' ' << (angle.hasMaxLimit ? 1 : 0) << ' ' << angle.maxDegrees << "\n";
    }
    output << "END\n";
    output.flush();
    if (!output) {
        MessageBoxW(hWnd, L"An error occurred while writing the model file.", L"Save model",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    g_currentFilePath = path;
    g_dirty = false;
    UpdateWindowTitle(hWnd);
    return true;
}

bool SaveModel(HWND hWnd, bool saveAs) {
    std::wstring path = g_currentFilePath;
    if (saveAs || path.empty()) {
        std::vector<wchar_t> fileBuffer(32768, L'\0');
        if (!path.empty()) wcsncpy_s(fileBuffer.data(), fileBuffer.size(), path.c_str(), _TRUNCATE);
        static const wchar_t filter[] =
            L"Kinematics model (*.kin)\0*.kin\0All files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hWnd;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrDefExt = L"kin";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
        if (!GetSaveFileNameW(&dialog)) return false;
        path = fileBuffer.data();
    }
    return SaveModelToFile(hWnd, path);
}

bool ConfirmDiscardChanges(HWND hWnd) {
    if (!g_dirty) return true;
    const int choice = MessageBoxW(hWnd, L"Save changes to the current model?", L"Unsaved changes",
                                   MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON1);
    if (choice == IDCANCEL) return false;
    if (choice == IDYES) return SaveModel(hWnd, false);
    return true;
}

bool LoadModelFromFile(HWND hWnd, const std::wstring& path, bool showErrors) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    auto fail = [&](const wchar_t* detail) {
        if (showErrors) {
            const std::wstring message = L"This is not a valid Kinematics model.\n\n" + std::wstring(detail);
            MessageBoxW(hWnd, message.c_str(), L"Open model", MB_OK | MB_ICONERROR);
        }
        return false;
    };
    if (!input) return fail(L"The file could not be opened.");

    std::string token;
    int version = 0;
    if (!(input >> token >> version) || token != "KINEMATICS_MODEL" || (version != 1 && version != 2))
        return fail(L"The header or format version is unsupported.");

    double zoom = 0.0, panX = 0.0, panY = 0.0;
    if (!(input >> token >> zoom >> panX >> panY) || token != "VIEW" ||
        !std::isfinite(zoom) || !std::isfinite(panX) || !std::isfinite(panY) || zoom <= 0.0)
        return fail(L"The saved view is invalid.");

    int snap = 0, defaultPointFixed = 0, defaultLinkFixed = 0;
    if (!(input >> token >> snap >> defaultPointFixed >> defaultLinkFixed) || token != "SETTINGS" ||
        (snap != 0 && snap != 1) || (defaultPointFixed != 0 && defaultPointFixed != 1) ||
        (defaultLinkFixed != 0 && defaultLinkFixed != 1))
        return fail(L"The saved editor settings are invalid.");

    size_t pointCount = 0;
    if (!(input >> token >> pointCount) || token != "POINTS" || pointCount > 100000)
        return fail(L"The point list is invalid or too large.");
    std::vector<ModelPoint> points;
    std::set<int> pointIds;
    int highestPointId = 0;
    points.reserve(pointCount);
    for (size_t index = 0; index < pointCount; ++index) {
        ModelPoint point;
        int fixed = 0;
        if (!(input >> token >> point.id >> point.position.x >> point.position.y >> fixed) ||
            token != "POINT" || point.id <= 0 || (fixed != 0 && fixed != 1) ||
            !std::isfinite(point.position.x) || !std::isfinite(point.position.y) ||
            !pointIds.insert(point.id).second)
            return fail(L"A point record is invalid or duplicated.");
        point.fixed = fixed != 0;
        highestPointId = (std::max)(highestPointId, point.id);
        points.push_back(point);
    }

    size_t linkCount = 0;
    if (!(input >> token >> linkCount) || token != "LINKS" || linkCount > 200000)
        return fail(L"The link list is invalid or too large.");
    std::vector<ModelLink> links;
    std::set<int> linkIds;
    int highestLinkId = 0;
    links.reserve(linkCount);
    for (size_t index = 0; index < linkCount; ++index) {
        ModelLink link;
        int fixed = 0;
        const bool baseRecordValid = static_cast<bool>(
            input >> token >> link.id >> link.pointA >> link.pointB >> fixed >> link.targetLength);
        const bool limitsValid = version == 1 || static_cast<bool>(input >> link.minLength >> link.maxLength);
        if (!baseRecordValid || !limitsValid ||
            token != "LINK" || link.id <= 0 || link.pointA == link.pointB ||
            pointIds.count(link.pointA) == 0 || pointIds.count(link.pointB) == 0 ||
            (fixed != 0 && fixed != 1) || !std::isfinite(link.targetLength) ||
            !std::isfinite(link.minLength) || !std::isfinite(link.maxLength) ||
            link.targetLength < 0.0 || link.minLength < 0.0 || link.maxLength < 0.0 ||
            (link.maxLength > 0.0 && link.minLength > link.maxLength) ||
            !linkIds.insert(link.id).second)
            return fail(L"A link record is invalid, duplicated, or references a missing point.");
        link.fixedLength = fixed != 0;
        highestLinkId = (std::max)(highestLinkId, link.id);
        links.push_back(link);
    }

    size_t angleCount = 0;
    if (!(input >> token >> angleCount) || token != "ANGLES" || angleCount > 100000)
        return fail(L"The angle list is invalid or too large.");
    std::vector<AngleMeasurement> angles;
    std::set<int> angleIds;
    int highestAngleId = 0;
    angles.reserve(angleCount);
    for (size_t index = 0; index < angleCount; ++index) {
        AngleMeasurement angle;
        int hasMin = 0, hasMax = 0;
        const bool baseRecordValid = static_cast<bool>(
            input >> token >> angle.id >> angle.pointA >> angle.vertex >> angle.pointC);
        const bool limitsValid = version == 1 || static_cast<bool>(
            input >> hasMin >> angle.minDegrees >> hasMax >> angle.maxDegrees);
        if (!baseRecordValid || !limitsValid ||
            token != "ANGLE" || angle.id <= 0 || angle.pointA == angle.vertex ||
            angle.pointA == angle.pointC || angle.vertex == angle.pointC ||
            pointIds.count(angle.pointA) == 0 || pointIds.count(angle.vertex) == 0 ||
            pointIds.count(angle.pointC) == 0 || (hasMin != 0 && hasMin != 1) ||
            (hasMax != 0 && hasMax != 1) || !std::isfinite(angle.minDegrees) ||
            !std::isfinite(angle.maxDegrees) || angle.minDegrees < 0.0 || angle.minDegrees > 180.0 ||
            angle.maxDegrees < 0.0 || angle.maxDegrees > 180.0 ||
            (hasMin && hasMax && angle.minDegrees > angle.maxDegrees) ||
            !angleIds.insert(angle.id).second)
            return fail(L"An angle record is invalid, duplicated, or references a missing point.");
        angle.hasMinLimit = hasMin != 0;
        angle.hasMaxLimit = hasMax != 0;
        highestAngleId = (std::max)(highestAngleId, angle.id);
        angles.push_back(angle);
    }
    if (!(input >> token) || token != "END") return fail(L"The file is incomplete.");

    g_points = std::move(points);
    g_links = std::move(links);
    g_angles = std::move(angles);
    g_nextPointId = highestPointId + 1;
    g_nextLinkId = highestLinkId + 1;
    g_nextAngleId = highestAngleId + 1;
    g_zoom = (std::max)(0.25, (std::min)(8.0, zoom));
    g_panX = panX;
    g_panY = panY;
    g_snapToGrid = snap != 0;
    g_defaultPointFixed = defaultPointFixed != 0;
    g_defaultLinkFixed = defaultLinkFixed != 0;
    g_selection = {};
    ClearPendingSteps();
    g_currentFilePath = path;
    g_dirty = false;
    SyncProperties(hWnd);
    UpdateWindowTitle(hWnd);
    return true;
}

void OpenModel(HWND hWnd) {
    std::vector<wchar_t> fileBuffer(32768, L'\0');
    static const wchar_t filter[] =
        L"Kinematics model (*.kin)\0*.kin\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hWnd;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.lpstrDefExt = L"kin";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return;
    if (!ConfirmDiscardChanges(hWnd)) return;
    LoadModelFromFile(hWnd, fileBuffer.data(), true);
}

void DeleteSelection(HWND hWnd) {
    if (g_selection.kind == SelectionKind::None) return;
    if (g_selection.kind == SelectionKind::Point) {
        const int id = g_selection.id;
        g_points.erase(std::remove_if(g_points.begin(), g_points.end(),
            [id](const ModelPoint& point) { return point.id == id; }), g_points.end());
        g_links.erase(std::remove_if(g_links.begin(), g_links.end(),
            [id](const ModelLink& link) { return link.pointA == id || link.pointB == id; }), g_links.end());
        g_angles.erase(std::remove_if(g_angles.begin(), g_angles.end(),
            [id](const AngleMeasurement& angle) {
                return angle.pointA == id || angle.vertex == id || angle.pointC == id;
            }), g_angles.end());
    } else if (g_selection.kind == SelectionKind::Link) {
        const int id = g_selection.id;
        g_links.erase(std::remove_if(g_links.begin(), g_links.end(),
            [id](const ModelLink& link) { return link.id == id; }), g_links.end());
    } else if (g_selection.kind == SelectionKind::Angle) {
        const int id = g_selection.id;
        g_angles.erase(std::remove_if(g_angles.begin(), g_angles.end(),
            [id](const AngleMeasurement& angle) { return angle.id == id; }), g_angles.end());
    }
    g_selection = {};
    ClearPendingSteps();
    MarkDirty(hWnd);
    SyncProperties(hWnd);
}

void ClearModel(HWND hWnd, bool confirm) {
    if (confirm && !ConfirmDiscardChanges(hWnd)) return;
    g_points.clear();
    g_links.clear();
    g_angles.clear();
    g_nextPointId = g_nextLinkId = g_nextAngleId = 1;
    g_selection = {};
    ClearPendingSteps();
    g_currentFilePath.clear();
    g_dirty = false;
    SyncProperties(hWnd);
    UpdateWindowTitle(hWnd);
}

void ResetView(HWND hWnd) {
    g_zoom = 2.0;
    g_panX = g_panY = 0.0;
    MarkDirty(hWnd);
    InvalidateRect(hWnd, nullptr, FALSE);
}

void ShowControlsHelp(HWND hWnd) {
    MessageBoxW(hWnd,
        L"BUILD A LINKAGE\n"
        L"1. Point: click the grid to add pivots.\n"
        L"2. Link: click two points to join them.\n"
        L"3. Select: drag a moving point to simulate the mechanism.\n"
        L"4. Angle: click point, vertex, point to measure an angle.\n\n"
        L"NAVIGATION\n"
        L"Mouse wheel: zoom around the cursor\n"
        L"Right or middle drag: pan\n"
        L"Delete: remove the selected item\n"
        L"Escape: cancel the current operation\n"
        L"V / P / L / A: switch tools\n\n"
        L"FILES\n"
        L"Ctrl+O: open a model\n"
        L"Ctrl+S: save   |   Ctrl+Shift+S: save as\n\n"
        L"HARD LIMITS\n"
        L"Select a non-fixed link to set minimum/maximum travel.\n"
        L"Select an angle label to set minimum/maximum degrees.\n"
        L"Leave a limit blank to keep that direction unrestricted.\n\n"
        L"Coordinates are millimetres; positive Y points upward.",
        L"Kinematics controls", MB_OK | MB_ICONINFORMATION);
}

HWND CreateControl(HWND parent, DWORD exStyle, const wchar_t* className, const wchar_t* text,
                   DWORD style, int id) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 100, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
    if (control && g_uiFont) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    return control;
}

void CreateEditorControls(HWND hWnd) {
    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_panelBrush = CreateSolidBrush(kPanel);
    g_editBrush = CreateSolidBrush(RGB(21, 26, 33));

    g_controls.summary = CreateControl(hWnd, 0, L"STATIC", L"Nothing selected", SS_LEFT | SS_NOPREFIX, IDC_SELECTION_SUMMARY);
    g_controls.snap = CreateControl(hWnd, 0, L"BUTTON", L"Snap to 10 mm grid", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SNAP);
    g_controls.pointFixed = CreateControl(hWnd, 0, L"BUTTON", L"Fixed / grounded point", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_POINT_FIXED);
    g_controls.pointXLabel = CreateControl(hWnd, 0, L"STATIC", L"X (mm)", SS_LEFT, 0);
    g_controls.pointX = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_POINT_X);
    g_controls.pointYLabel = CreateControl(hWnd, 0, L"STATIC", L"Y (mm)", SS_LEFT, 0);
    g_controls.pointY = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_POINT_Y);
    g_controls.applyPoint = CreateControl(hWnd, 0, L"BUTTON", L"Apply coordinates", BS_OWNERDRAW | WS_TABSTOP, IDC_APPLY_POINT);
    g_controls.linkFixed = CreateControl(hWnd, 0, L"BUTTON", L"Fixed length constraint", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_LINK_FIXED);
    g_controls.linkLengthLabel = CreateControl(hWnd, 0, L"STATIC", L"Length (mm)", SS_LEFT, 0);
    g_controls.linkLength = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_LINK_LENGTH);
    g_controls.linkMinLabel = CreateControl(hWnd, 0, L"STATIC", L"Min (mm)", SS_LEFT, 0);
    g_controls.linkMin = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_LINK_MIN);
    g_controls.linkMaxLabel = CreateControl(hWnd, 0, L"STATIC", L"Max (mm)", SS_LEFT, 0);
    g_controls.linkMax = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_LINK_MAX);
    g_controls.applyLink = CreateControl(hWnd, 0, L"BUTTON", L"Apply link settings", BS_OWNERDRAW | WS_TABSTOP, IDC_APPLY_LINK);
    g_controls.angleMinLabel = CreateControl(hWnd, 0, L"STATIC", L"Min (deg)", SS_LEFT, 0);
    g_controls.angleMin = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_ANGLE_MIN);
    g_controls.angleMaxLabel = CreateControl(hWnd, 0, L"STATIC", L"Max (deg)", SS_LEFT, 0);
    g_controls.angleMax = CreateControl(hWnd, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_ANGLE_MAX);
    g_controls.applyAngle = CreateControl(hWnd, 0, L"BUTTON", L"Apply angle limits", BS_OWNERDRAW | WS_TABSTOP, IDC_APPLY_ANGLE);
    g_controls.deleteSelected = CreateControl(hWnd, 0, L"BUTTON", L"Delete selected", BS_OWNERDRAW | WS_TABSTOP, IDC_DELETE_SELECTED);
    g_controls.clearModel = CreateControl(hWnd, 0, L"BUTTON", L"New / clear model", BS_OWNERDRAW | WS_TABSTOP, IDC_CLEAR_MODEL);
    g_controls.resetView = CreateControl(hWnd, 0, L"BUTTON", L"Reset view", BS_OWNERDRAW | WS_TABSTOP, IDC_RESET_VIEW_BUTTON);
    SendMessageW(g_controls.snap, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_controls.linkFixed, BM_SETCHECK, BST_CHECKED, 0);
}

void LayoutControls(HWND hWnd) {
    RECT client = {};
    GetClientRect(hWnd, &client);
    const int panelX = (std::max)(0L, client.right - kSidebarWidth);
    const int x = panelX + 20;
    const int width = kSidebarWidth - 40;
    MoveWindow(g_controls.summary, x, 82, width, 54, TRUE);
    MoveWindow(g_controls.snap, x, 151, width, 26, TRUE);
    MoveWindow(g_controls.pointFixed, x, 215, width, 26, TRUE);
    MoveWindow(g_controls.pointXLabel, x, 254, 65, 24, TRUE);
    MoveWindow(g_controls.pointX, x + 78, 250, width - 78, 28, TRUE);
    MoveWindow(g_controls.pointYLabel, x, 290, 65, 24, TRUE);
    MoveWindow(g_controls.pointY, x + 78, 286, width - 78, 28, TRUE);
    MoveWindow(g_controls.applyPoint, x, 324, width, 31, TRUE);
    MoveWindow(g_controls.linkFixed, x, 403, width, 26, TRUE);
    MoveWindow(g_controls.linkLengthLabel, x, 435, 104, 24, TRUE);
    MoveWindow(g_controls.linkLength, x + 116, 431, width - 116, 28, TRUE);
    MoveWindow(g_controls.linkMinLabel, x, 469, 104, 24, TRUE);
    MoveWindow(g_controls.linkMin, x + 116, 465, width - 116, 28, TRUE);
    MoveWindow(g_controls.linkMaxLabel, x, 503, 104, 24, TRUE);
    MoveWindow(g_controls.linkMax, x + 116, 499, width - 116, 28, TRUE);
    MoveWindow(g_controls.applyLink, x, 533, width, 31, TRUE);
    MoveWindow(g_controls.angleMinLabel, x, 607, 104, 24, TRUE);
    MoveWindow(g_controls.angleMin, x + 116, 603, width - 116, 28, TRUE);
    MoveWindow(g_controls.angleMaxLabel, x, 641, 104, 24, TRUE);
    MoveWindow(g_controls.angleMax, x + 116, 637, width - 116, 28, TRUE);
    MoveWindow(g_controls.applyAngle, x, 671, width, 31, TRUE);
    MoveWindow(g_controls.deleteSelected, x, 719, width, 31, TRUE);
    MoveWindow(g_controls.clearModel, x, 758, (width - 8) / 2, 31, TRUE);
    MoveWindow(g_controls.resetView, x + (width + 8) / 2, 758, (width - 8) / 2, 31, TRUE);
}

void SyncProperties(HWND hWnd) {
    g_syncingControls = true;
    SendMessageW(g_controls.snap, BM_SETCHECK, g_snapToGrid ? BST_CHECKED : BST_UNCHECKED, 0);
    ModelPoint* point = (g_selection.kind == SelectionKind::Point) ? FindPoint(g_selection.id) : nullptr;
    ModelLink* link = (g_selection.kind == SelectionKind::Link) ? FindLink(g_selection.id) : nullptr;
    const AngleMeasurement* angle = (g_selection.kind == SelectionKind::Angle) ? FindAngleConst(g_selection.id) : nullptr;
    std::wstring summary;

    if (point) {
        summary = PointName(point->id) + (point->fixed ? L"  -  fixed pivot" : L"  -  moving pivot");
        SetText(g_controls.pointX, FormatNumber(point->position.x, 2));
        SetText(g_controls.pointY, FormatNumber(point->position.y, 2));
        SendMessageW(g_controls.pointFixed, BM_SETCHECK, point->fixed ? BST_CHECKED : BST_UNCHECKED, 0);
    } else {
        SetWindowTextW(g_controls.pointX, L"");
        SetWindowTextW(g_controls.pointY, L"");
        SendMessageW(g_controls.pointFixed, BM_SETCHECK, g_defaultPointFixed ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    if (link) {
        summary = L"Link " + std::to_wstring(link->id) + L"  " + PointName(link->pointA) + L" - " + PointName(link->pointB);
        SetText(g_controls.linkLength, FormatNumber(link->fixedLength ? link->targetLength : CurrentLinkLength(*link), 2));
        SetText(g_controls.linkMin, link->minLength > 0.0 ? FormatNumber(link->minLength, 2) : L"");
        SetText(g_controls.linkMax, link->maxLength > 0.0 ? FormatNumber(link->maxLength, 2) : L"");
        SendMessageW(g_controls.linkFixed, BM_SETCHECK, link->fixedLength ? BST_CHECKED : BST_UNCHECKED, 0);
    } else {
        SetWindowTextW(g_controls.linkLength, L"");
        SetWindowTextW(g_controls.linkMin, L"");
        SetWindowTextW(g_controls.linkMax, L"");
        SendMessageW(g_controls.linkFixed, BM_SETCHECK, g_defaultLinkFixed ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    if (angle) {
        summary = L"Angle " + PointName(angle->pointA) + L" - " + PointName(angle->vertex) + L" - " +
                  PointName(angle->pointC) + L"\r\n" + FormatNumber(AngleDegrees(*angle), 1) + L" degrees";
        SetText(g_controls.angleMin, angle->hasMinLimit ? FormatNumber(angle->minDegrees, 2) : L"");
        SetText(g_controls.angleMax, angle->hasMaxLimit ? FormatNumber(angle->maxDegrees, 2) : L"");
    } else {
        SetWindowTextW(g_controls.angleMin, L"");
        SetWindowTextW(g_controls.angleMax, L"");
    }
    if (summary.empty()) {
        switch (g_tool) {
        case Tool::Point: summary = L"Click the grid to add a point."; break;
        case Tool::Link: summary = L"Click two points to create a link."; break;
        case Tool::Angle: summary = L"Click point, vertex, point."; break;
        default: summary = L"Select an item to edit its properties."; break;
        }
    }
    SetText(g_controls.summary, summary);
    EnableWindow(g_controls.pointX, point != nullptr);
    EnableWindow(g_controls.pointY, point != nullptr);
    EnableWindow(g_controls.applyPoint, point != nullptr);
    EnableWindow(g_controls.linkLength, link != nullptr && link->fixedLength);
    EnableWindow(g_controls.linkMin, link != nullptr && !link->fixedLength);
    EnableWindow(g_controls.linkMax, link != nullptr && !link->fixedLength);
    EnableWindow(g_controls.applyLink, link != nullptr);
    EnableWindow(g_controls.angleMin, angle != nullptr);
    EnableWindow(g_controls.angleMax, angle != nullptr);
    EnableWindow(g_controls.applyAngle, angle != nullptr);
    EnableWindow(g_controls.deleteSelected, g_selection.kind != SelectionKind::None);
    g_syncingControls = false;
    InvalidateRect(hWnd, nullptr, FALSE);
}

RECT ToolButtonRect(int index) {
    const int left = 18 + index * 116;
    return { left, 10, left + 106, 48 };
}

RECT FileButtonRect(int index) {
    const int left = 498 + index * 90;
    return { left, 10, left + 80, 48 };
}

void DrawTextInRect(HDC dc, const std::wstring& text, RECT rect, UINT format, COLORREF colour, HFONT font) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}

void DrawToolbarButton(HDC dc, int index, const wchar_t* shortcut, const wchar_t* label, bool active) {
    const RECT rect = ToolButtonRect(index);
    HBRUSH brush = CreateSolidBrush(active ? RGB(30, 105, 145) : RGB(27, 34, 44));
    HPEN pen = CreatePen(PS_SOLID, 1, active ? kBlue : kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    RECT keyRect = { rect.left + 9, rect.top + 8, rect.left + 29, rect.bottom - 8 };
    DrawTextInRect(dc, shortcut, keyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE, active ? RGB(230, 249, 255) : kBlue, g_uiFont);
    RECT labelRect = { rect.left + 32, rect.top, rect.right - 6, rect.bottom };
    DrawTextInRect(dc, label, labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText, g_uiFont);
}

void DrawFileButton(HDC dc, int index, const wchar_t* label) {
    const RECT rect = FileButtonRect(index);
    HBRUSH brush = CreateSolidBrush(RGB(27, 34, 44));
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    DrawTextInRect(dc, label, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE, kText, g_uiFont);
}

void DrawGrid(HWND hWnd, HDC dc, const RECT& canvas) {
    HBRUSH background = CreateSolidBrush(kWindow);
    FillRect(dc, &canvas, background);
    DeleteObject(background);
    const Vec2 topLeft = ScreenToWorld(hWnd, { canvas.left, canvas.top });
    const Vec2 bottomRight = ScreenToWorld(hWnd, { canvas.right, canvas.bottom });
    const int minX = static_cast<int>(std::floor(topLeft.x / kGridMillimetres)) - 1;
    const int maxX = static_cast<int>(std::ceil(bottomRight.x / kGridMillimetres)) + 1;
    const int minY = static_cast<int>(std::floor(bottomRight.y / kGridMillimetres)) - 1;
    const int maxY = static_cast<int>(std::ceil(topLeft.y / kGridMillimetres)) + 1;

    // Every grid pen, including major lines and axes, remains exactly one pixel.
    HPEN minorPen = CreatePen(PS_SOLID, 1, RGB(30, 37, 46));
    HPEN majorPen = CreatePen(PS_SOLID, 1, RGB(45, 55, 67));
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(76, 90, 106));
    for (int gridX = minX; gridX <= maxX; ++gridX) {
        if (g_zoom * kGridMillimetres < 5.0 && gridX % 5 != 0) continue;
        const POINT screen = WorldToScreen(hWnd, { gridX * kGridMillimetres, 0.0 });
        HPEN chosen = gridX == 0 ? axisPen : (gridX % 5 == 0 ? majorPen : minorPen);
        HGDIOBJ oldPen = SelectObject(dc, chosen);
        MoveToEx(dc, screen.x, canvas.top, nullptr);
        LineTo(dc, screen.x, canvas.bottom);
        SelectObject(dc, oldPen);
    }
    for (int gridY = minY; gridY <= maxY; ++gridY) {
        if (g_zoom * kGridMillimetres < 5.0 && gridY % 5 != 0) continue;
        const POINT screen = WorldToScreen(hWnd, { 0.0, gridY * kGridMillimetres });
        HPEN chosen = gridY == 0 ? axisPen : (gridY % 5 == 0 ? majorPen : minorPen);
        HGDIOBJ oldPen = SelectObject(dc, chosen);
        MoveToEx(dc, canvas.left, screen.y, nullptr);
        LineTo(dc, canvas.right, screen.y);
        SelectObject(dc, oldPen);
    }
    DeleteObject(axisPen);
    DeleteObject(majorPen);
    DeleteObject(minorPen);
    const POINT origin = WorldToScreen(hWnd, { 0.0, 0.0 });
    RECT label = { origin.x + 6, origin.y + 5, origin.x + 70, origin.y + 24 };
    DrawTextInRect(dc, L"0, 0 mm", label, DT_LEFT | DT_TOP | DT_SINGLELINE, kMutedText, g_smallFont);
}

RECT DrawLabelBubble(HDC dc, const std::wstring& text, POINT preferredCentre,
                     COLORREF foreground, bool selected = false) {
    SIZE size = {};
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, g_smallFont));
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    const int width = size.cx + 12;
    const int height = size.cy + 6;
    RECT rect = {};
    bool placed = false;
    const int columnStep = width + 8;
    const int rowStep = height + 4;
    for (int columnIndex = 0; columnIndex < 5 && !placed; ++columnIndex) {
        const int column = columnIndex == 0 ? 0 : ((columnIndex + 1) / 2) * (columnIndex & 1 ? 1 : -1);
        for (int rowIndex = 0; rowIndex < 17 && !placed; ++rowIndex) {
            const int row = rowIndex == 0 ? 0 : ((rowIndex + 1) / 2) * (rowIndex & 1 ? -1 : 1);
            const POINT centre = { preferredCentre.x + column * columnStep,
                                   preferredCentre.y + row * rowStep };
            RECT candidate = { centre.x - width / 2, centre.y - height / 2,
                               centre.x - width / 2 + width, centre.y - height / 2 + height };
            if (candidate.left < g_labelArea.left + 3) OffsetRect(&candidate, g_labelArea.left + 3 - candidate.left, 0);
            if (candidate.right > g_labelArea.right - 3) OffsetRect(&candidate, g_labelArea.right - 3 - candidate.right, 0);
            if (candidate.top < g_labelArea.top + 3) OffsetRect(&candidate, 0, g_labelArea.top + 3 - candidate.top);
            if (candidate.bottom > g_labelArea.bottom - 3) OffsetRect(&candidate, 0, g_labelArea.bottom - 3 - candidate.bottom);
            bool overlaps = false;
            RECT padded = candidate;
            InflateRect(&padded, 2, 2);
            for (const RECT& existing : g_labelBounds) {
                RECT intersection = {};
                if (IntersectRect(&intersection, &padded, &existing)) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                rect = candidate;
                placed = true;
            }
        }
    }
    if (!placed) {
        rect = { preferredCentre.x - width / 2, preferredCentre.y - height / 2,
                 preferredCentre.x - width / 2 + width, preferredCentre.y - height / 2 + height };
    }
    g_labelBounds.push_back(rect);
    HBRUSH brush = CreateSolidBrush(selected ? RGB(47, 47, 66) : RGB(28, 34, 43));
    HPEN pen = CreatePen(PS_SOLID, 1, selected ? foreground : kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, foreground);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    return rect;
}

void DrawLinks(HWND hWnd, HDC dc) {
    for (const ModelLink& link : g_links) {
        const ModelPoint* a = FindPointConst(link.pointA);
        const ModelPoint* b = FindPointConst(link.pointB);
        if (!a || !b) continue;
        const POINT screenA = WorldToScreen(hWnd, a->position);
        const POINT screenB = WorldToScreen(hWnd, b->position);
        const bool selected = g_selection.kind == SelectionKind::Link && g_selection.id == link.id;
        const double currentLength = CurrentLinkLength(link);
        const bool atTravelLimit = !link.fixedLength &&
            ((link.minLength > 0.0 && std::abs(currentLength - link.minLength) < 0.01) ||
             (link.maxLength > 0.0 && std::abs(currentLength - link.maxLength) < 0.01));
        if (selected) {
            HPEN glow = CreatePen(PS_SOLID, 8, RGB(43, 79, 98));
            HGDIOBJ oldPen = SelectObject(dc, glow);
            MoveToEx(dc, screenA.x, screenA.y, nullptr);
            LineTo(dc, screenB.x, screenB.y);
            SelectObject(dc, oldPen);
            DeleteObject(glow);
        }
        const COLORREF linkColour = link.fixedLength ? kBlue : (atTravelLimit ? RGB(245, 99, 91) : kOrange);
        HPEN pen = CreatePen(link.fixedLength ? PS_SOLID : PS_DASH, link.fixedLength ? 4 : 1,
                             linkColour);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, screenA.x, screenA.y, nullptr);
        LineTo(dc, screenB.x, screenB.y);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        const POINT middle = { (screenA.x + screenB.x) / 2, (screenA.y + screenB.y) / 2 - 14 };
        std::wstring text = FormatNumber(currentLength, 1) + L" mm";
        if (link.fixedLength) text += L"  fixed";
        else if (link.minLength > 0.0 || link.maxLength > 0.0) {
            text += L"  [";
            text += link.minLength > 0.0 ? FormatNumber(link.minLength, 1) : L"0";
            text += L" - ";
            text += link.maxLength > 0.0 ? FormatNumber(link.maxLength, 1) : L"inf";
            text += L"]";
        }
        const COLORREF labelColour = link.fixedLength ? RGB(133, 220, 255) :
            (atTravelLimit ? RGB(255, 151, 145) : RGB(255, 190, 126));
        DrawLabelBubble(dc, text, middle, labelColour, selected);
    }
}

void DrawAngles(HWND hWnd, HDC dc) {
    for (const AngleMeasurement& angle : g_angles) {
        const ModelPoint* a = FindPointConst(angle.pointA);
        const ModelPoint* b = FindPointConst(angle.vertex);
        const ModelPoint* c = FindPointConst(angle.pointC);
        if (!a || !b || !c) continue;
        double start = std::atan2(a->position.y - b->position.y, a->position.x - b->position.x);
        double finish = std::atan2(c->position.y - b->position.y, c->position.x - b->position.x);
        double sweep = finish - start;
        while (sweep > kPi) sweep -= 2.0 * kPi;
        while (sweep < -kPi) sweep += 2.0 * kPi;
        POINT arc[25] = {};
        for (int i = 0; i <= 24; ++i) {
            const double value = start + sweep * (static_cast<double>(i) / 24.0);
            arc[i] = WorldToScreen(hWnd, b->position + Vec2{ std::cos(value), std::sin(value) } * (22.0 / g_zoom));
        }
        const bool selected = g_selection.kind == SelectionKind::Angle && g_selection.id == angle.id;
        const double currentDegrees = AngleDegrees(angle);
        const bool atAngleLimit =
            (angle.hasMinLimit && std::abs(currentDegrees - angle.minDegrees) < 0.01) ||
            (angle.hasMaxLimit && std::abs(currentDegrees - angle.maxDegrees) < 0.01);
        const COLORREF angleColour = atAngleLimit ? RGB(245, 99, 91) : kPurple;
        HPEN pen = CreatePen(PS_SOLID, selected ? 4 : 2, angleColour);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        Polyline(dc, arc, 25);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        std::wstring text = FormatNumber(currentDegrees, 1) + L" deg";
        if (angle.hasMinLimit || angle.hasMaxLimit) {
            text += L"  [";
            text += angle.hasMinLimit ? FormatNumber(angle.minDegrees, 1) : L"0";
            text += L" - ";
            text += angle.hasMaxLimit ? FormatNumber(angle.maxDegrees, 1) : L"180";
            text += L"]";
        }
        const RECT label = DrawLabelBubble(dc, text, AngleLabelPosition(hWnd, angle),
                                           atAngleLimit ? RGB(255, 151, 145) : RGB(221, 169, 255), selected);
        g_angleHitBounds.push_back({ angle.id, label });
    }
}

void DrawGround(HDC dc, POINT centre) {
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(174, 191, 207));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, centre.x - 11, centre.y + 11, nullptr);
    LineTo(dc, centre.x + 11, centre.y + 11);
    for (int x = -9; x <= 9; x += 6) {
        MoveToEx(dc, centre.x + x, centre.y + 11, nullptr);
        LineTo(dc, centre.x + x - 5, centre.y + 17);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawPoints(HWND hWnd, HDC dc) {
    for (const ModelPoint& point : g_points) {
        const POINT centre = WorldToScreen(hWnd, point.position);
        const bool selected = g_selection.kind == SelectionKind::Point && g_selection.id == point.id;
        const bool pending = point.id == g_linkStartPointId ||
            std::find(g_anglePointIds.begin(), g_anglePointIds.end(), point.id) != g_anglePointIds.end();
        if (point.fixed) DrawGround(dc, centre);
        if (selected || pending) {
            HBRUSH ring = CreateSolidBrush(selected ? RGB(52, 117, 145) : RGB(92, 62, 113));
            HGDIOBJ oldBrush = SelectObject(dc, ring);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, centre.x - 11, centre.y - 11, centre.x + 12, centre.y + 12);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(ring);
        }
        HBRUSH brush = CreateSolidBrush(point.fixed ? RGB(202, 218, 231) : RGB(22, 28, 35));
        HPEN pen = CreatePen(PS_SOLID, 2, point.fixed ? RGB(226, 238, 247) : kBlue);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        Ellipse(dc, centre.x - 7, centre.y - 7, centre.x + 8, centre.y + 8);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        if (point.fixed) {
            HBRUSH centreBrush = CreateSolidBrush(RGB(29, 52, 67));
            HGDIOBJ old = SelectObject(dc, centreBrush);
            Ellipse(dc, centre.x - 2, centre.y - 2, centre.x + 3, centre.y + 3);
            SelectObject(dc, old);
            DeleteObject(centreBrush);
        }
        const std::wstring label = PointName(point.id) + L"  (" + FormatNumber(point.position.x, 1) +
            L", " + FormatNumber(point.position.y, 1) + L")";
        DrawLabelBubble(dc, label, { centre.x + 72, centre.y - 18 },
                        RGB(196, 207, 218), selected);
    }
}

void DrawPreview(HWND hWnd, HDC dc, const RECT& canvas) {
    if (!Contains(canvas, g_lastMouse)) return;
    if (g_tool == Tool::Point) {
        const POINT preview = WorldToScreen(hWnd, SnapPosition(ScreenToWorld(hWnd, g_lastMouse)));
        HPEN pen = CreatePen(PS_DOT, 1, kBlue);
        HBRUSH brush = CreateSolidBrush(RGB(27, 51, 65));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        Ellipse(dc, preview.x - 7, preview.y - 7, preview.x + 8, preview.y + 8);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    } else if (g_tool == Tool::Link && g_linkStartPointId) {
        const ModelPoint* start = FindPointConst(g_linkStartPointId);
        if (start) {
            const POINT first = WorldToScreen(hWnd, start->position);
            HPEN pen = CreatePen(PS_DASH, 1, kBlue);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, first.x, first.y, nullptr);
            LineTo(dc, g_lastMouse.x, g_lastMouse.y);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
    } else if (g_tool == Tool::Angle && !g_anglePointIds.empty()) {
        HPEN pen = CreatePen(PS_DASH, 1, kPurple);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        for (size_t i = 1; i < g_anglePointIds.size(); ++i) {
            const ModelPoint* previous = FindPointConst(g_anglePointIds[i - 1]);
            const ModelPoint* current = FindPointConst(g_anglePointIds[i]);
            if (previous && current) {
                const POINT a = WorldToScreen(hWnd, previous->position);
                const POINT b = WorldToScreen(hWnd, current->position);
                MoveToEx(dc, a.x, a.y, nullptr);
                LineTo(dc, b.x, b.y);
            }
        }
        const ModelPoint* last = FindPointConst(g_anglePointIds.back());
        if (last) {
            const POINT screen = WorldToScreen(hWnd, last->position);
            MoveToEx(dc, screen.x, screen.y, nullptr);
            LineTo(dc, g_lastMouse.x, g_lastMouse.y);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
}

std::wstring StatusText() {
    switch (g_tool) {
    case Tool::Point: return L"POINT  |  Click to place a pivot; Fixed controls the new point";
    case Tool::Link: return g_linkStartPointId ? L"LINK  |  Choose the second endpoint (Esc cancels)" : L"LINK  |  Choose the first endpoint";
    case Tool::Angle:
        if (g_anglePointIds.empty()) return L"ANGLE  |  Choose the first point";
        if (g_anglePointIds.size() == 1) return L"ANGLE  |  Choose the vertex";
        return L"ANGLE  |  Choose the final point";
    default: return L"SELECT  |  Drag a moving point to simulate; wheel zooms; right-drag pans";
    }
}

void PaintEditor(HWND hWnd, HDC target) {
    RECT client = {};
    GetClientRect(hWnd, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HBRUSH windowBrush = CreateSolidBrush(kPanel);
    FillRect(dc, &client, windowBrush);
    DeleteObject(windowBrush);

    RECT toolbar = { 0, 0, client.right, kToolbarHeight };
    HBRUSH toolbarBrush = CreateSolidBrush(kToolbar);
    FillRect(dc, &toolbar, toolbarBrush);
    DeleteObject(toolbarBrush);
    DrawToolbarButton(dc, 0, L"V", L"Select", g_tool == Tool::Select);
    DrawToolbarButton(dc, 1, L"P", L"Point", g_tool == Tool::Point);
    DrawToolbarButton(dc, 2, L"L", L"Link", g_tool == Tool::Link);
    DrawToolbarButton(dc, 3, L"A", L"Angle", g_tool == Tool::Angle);
    DrawFileButton(dc, 0, L"Open");
    DrawFileButton(dc, 1, L"Save");
    RECT title = { 690, 7, client.right - kSidebarWidth - 18, 31 };
    DrawTextInRect(dc, L"KINEMATICS STUDIO", title, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, kText, g_titleFont);
    RECT subtitle = { 690, 30, client.right - kSidebarWidth - 18, 50 };
    DrawTextInRect(dc, L"2D linkage editor  |  millimetres", subtitle, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, kMutedText, g_smallFont);

    const RECT canvas = CanvasRect(hWnd);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);
    g_labelArea = canvas;
    g_labelBounds.clear();
    g_angleHitBounds.clear();
    DrawGrid(hWnd, dc, canvas);
    DrawLinks(hWnd, dc);
    DrawAngles(hWnd, dc);
    DrawPreview(hWnd, dc, canvas);
    DrawPoints(hWnd, dc);
    RestoreDC(dc, saved);

    RECT panel = { client.right - kSidebarWidth, kToolbarHeight, client.right, client.bottom - kStatusHeight };
    FillRect(dc, &panel, g_panelBrush);
    HPEN divider = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, panel.left, panel.top, nullptr);
    LineTo(dc, panel.left, panel.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(divider);
    RECT propertiesTitle = { panel.left + 20, 64, panel.right - 20, 84 };
    DrawTextInRect(dc, L"PROPERTIES", propertiesTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText, g_titleFont);
    RECT pointTitle = { panel.left + 20, 187, panel.right - 20, 210 };
    DrawTextInRect(dc, L"POINT", pointTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kMutedText, g_smallFont);
    RECT linkTitle = { panel.left + 20, 375, panel.right - 20, 398 };
    DrawTextInRect(dc, L"LINK", linkTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kMutedText, g_smallFont);
    RECT angleTitle = { panel.left + 20, 575, panel.right - 20, 598 };
    DrawTextInRect(dc, L"ANGLE LIMITS", angleTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kMutedText, g_smallFont);

    RECT status = { 0, client.bottom - kStatusHeight, client.right, client.bottom };
    HBRUSH statusBrush = CreateSolidBrush(RGB(22, 27, 34));
    FillRect(dc, &status, statusBrush);
    DeleteObject(statusBrush);
    RECT statusLeft = { 12, status.top, client.right - kSidebarWidth - 10, status.bottom };
    DrawTextInRect(dc, StatusText(), statusLeft, DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(174, 186, 199), g_smallFont);
    if (Contains(canvas, g_lastMouse)) {
        const Vec2 cursor = ScreenToWorld(hWnd, g_lastMouse);
        RECT coordinates = { client.right - kSidebarWidth, status.top, client.right - 12, status.bottom };
        DrawTextInRect(dc, L"X " + FormatNumber(cursor.x, 1) + L" mm     Y " + FormatNumber(cursor.y, 1) +
            L" mm     " + FormatNumber(g_zoom * 50.0, 0) + L"%", coordinates,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(174, 186, 199), g_smallFont);
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    HBRUSH brush = CreateSolidBrush(disabled ? RGB(35, 40, 48) : (pressed ? RGB(29, 96, 130) : kPanelRaised));
    HPEN pen = CreatePen(PS_SOLID, 1, focused ? kBlue : kBorder);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, 6, 6);
    SelectObject(item->hDC, oldPen);
    SelectObject(item->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    wchar_t text[128] = {};
    GetWindowTextW(item->hwndItem, text, 128);
    RECT rect = item->rcItem;
    if (pressed) OffsetRect(&rect, 0, 1);
    DrawTextInRect(item->hDC, text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                   disabled ? RGB(91, 100, 110) : kText, g_uiFont);
}

void ApplyPointProperties(HWND hWnd) {
    if (g_selection.kind != SelectionKind::Point) return;
    ModelPoint* point = FindPoint(g_selection.id);
    double x = 0.0, y = 0.0;
    if (!point || !ReadDouble(g_controls.pointX, x) || !ReadDouble(g_controls.pointY, y)) {
        MessageBoxW(hWnd, L"Enter valid numeric X and Y coordinates.", L"Coordinates", MB_OK | MB_ICONWARNING);
        return;
    }
    SolveConstraints(point->id, { x, y });
    MarkDirty(hWnd);
    SyncProperties(hWnd);
}

void ApplyLinkProperties(HWND hWnd) {
    if (g_selection.kind != SelectionKind::Link) return;
    ModelLink* link = FindLink(g_selection.id);
    if (!link) return;
    if (link->fixedLength) {
        double length = 0.0;
        if (!ReadDouble(g_controls.linkLength, length) || length <= 0.0) {
            MessageBoxW(hWnd, L"Enter a fixed length greater than zero.", L"Link length",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        link->targetLength = length;
    } else {
        bool hasMinimum = false, hasMaximum = false;
        double minimum = 0.0, maximum = 0.0;
        if (!ReadOptionalDouble(g_controls.linkMin, hasMinimum, minimum) ||
            !ReadOptionalDouble(g_controls.linkMax, hasMaximum, maximum) ||
            minimum < 0.0 || maximum < 0.0 || (hasMaximum && maximum <= 0.0) ||
            (hasMinimum && hasMaximum && minimum > maximum)) {
            MessageBoxW(hWnd,
                L"Enter a non-negative minimum and a maximum greater than zero, with the minimum no greater than the maximum.\n\n"
                L"Leave either box blank for no limit in that direction.",
                L"Link travel", MB_OK | MB_ICONWARNING);
            return;
        }
        link->minLength = hasMinimum ? minimum : 0.0;
        link->maxLength = hasMaximum ? maximum : 0.0;
    }
    ModelPoint* a = FindPoint(link->pointA);
    ModelPoint* b = FindPoint(link->pointB);
    if (a && b) {
        Vec2 direction = b->position - a->position;
        const double oldLength = Length(direction);
        if (oldLength < 1e-9) direction = { 1.0, 0.0 };
        else direction = direction * (1.0 / oldLength);
        Vec2 requested = b->position;
        if (link->fixedLength) requested = a->position + direction * link->targetLength;
        if (!b->fixed) SolveConstraints(b->id, requested);
        else if (!a->fixed) {
            Vec2 requestedA = a->position;
            if (link->fixedLength) requestedA = b->position - direction * link->targetLength;
            SolveConstraints(a->id, requestedA);
        }
    }
    MarkDirty(hWnd);
    SyncProperties(hWnd);
}

void ApplyAngleProperties(HWND hWnd) {
    if (g_selection.kind != SelectionKind::Angle) return;
    AngleMeasurement* angle = FindAngle(g_selection.id);
    if (!angle) return;
    bool hasMinimum = false, hasMaximum = false;
    double minimum = 0.0, maximum = 0.0;
    if (!ReadOptionalDouble(g_controls.angleMin, hasMinimum, minimum) ||
        !ReadOptionalDouble(g_controls.angleMax, hasMaximum, maximum) ||
        minimum < 0.0 || minimum > 180.0 || maximum < 0.0 || maximum > 180.0 ||
        (hasMinimum && hasMaximum && minimum > maximum)) {
        MessageBoxW(hWnd,
            L"Enter limits from 0 to 180 degrees, with the minimum no greater than the maximum.\n\n"
            L"Leave either box blank for no limit in that direction.",
            L"Angle limits", MB_OK | MB_ICONWARNING);
        return;
    }
    angle->hasMinLimit = hasMinimum;
    angle->minDegrees = minimum;
    angle->hasMaxLimit = hasMaximum;
    angle->maxDegrees = hasMaximum ? maximum : 180.0;

    ModelPoint* candidates[] = { FindPoint(angle->pointC), FindPoint(angle->pointA), FindPoint(angle->vertex) };
    for (ModelPoint* point : candidates) {
        if (point && !point->fixed) {
            SolveConstraints(point->id, point->position);
            break;
        }
    }
    MarkDirty(hWnd);
    SyncProperties(hWnd);
}

void SelectAt(HWND hWnd, POINT mouse) {
    const int pointId = HitTestPoint(hWnd, mouse);
    if (pointId) {
        g_selection = { SelectionKind::Point, pointId };
        ModelPoint* point = FindPoint(pointId);
        if (point && !point->fixed) {
            g_draggingPoint = true;
            g_draggedPointId = pointId;
            SetCapture(hWnd);
        }
        SyncProperties(hWnd);
        return;
    }
    const int angleId = HitTestAngle(hWnd, mouse);
    if (angleId) {
        g_selection = { SelectionKind::Angle, angleId };
        SyncProperties(hWnd);
        return;
    }
    const int linkId = HitTestLink(hWnd, mouse);
    g_selection = linkId ? Selection{ SelectionKind::Link, linkId } : Selection{};
    SyncProperties(hWnd);
}

void AddPointAt(HWND hWnd, POINT mouse) {
    ModelPoint point;
    point.id = g_nextPointId++;
    point.position = SnapPosition(ScreenToWorld(hWnd, mouse));
    point.fixed = g_defaultPointFixed;
    g_points.push_back(point);
    g_selection = { SelectionKind::Point, point.id };
    MarkDirty(hWnd);
    SyncProperties(hWnd);
}

void UseLinkTool(HWND hWnd, POINT mouse) {
    const int pointId = HitTestPoint(hWnd, mouse, 13.0);
    if (!pointId) return;
    if (!g_linkStartPointId) {
        g_linkStartPointId = pointId;
    } else if (g_linkStartPointId != pointId) {
        for (const ModelLink& existing : g_links) {
            if ((existing.pointA == g_linkStartPointId && existing.pointB == pointId) ||
                (existing.pointA == pointId && existing.pointB == g_linkStartPointId)) {
                g_selection = { SelectionKind::Link, existing.id };
                g_linkStartPointId = 0;
                SyncProperties(hWnd);
                return;
            }
        }
        const ModelPoint* a = FindPointConst(g_linkStartPointId);
        const ModelPoint* b = FindPointConst(pointId);
        if (a && b) {
            ModelLink link;
            link.id = g_nextLinkId++;
            link.pointA = a->id;
            link.pointB = b->id;
            link.fixedLength = g_defaultLinkFixed;
            link.targetLength = Length(b->position - a->position);
            g_links.push_back(link);
            g_selection = { SelectionKind::Link, link.id };
            MarkDirty(hWnd);
        }
        g_linkStartPointId = 0;
        SyncProperties(hWnd);
        return;
    }
    InvalidateRect(hWnd, nullptr, FALSE);
}

void UseAngleTool(HWND hWnd, POINT mouse) {
    const int pointId = HitTestPoint(hWnd, mouse, 13.0);
    if (!pointId || std::find(g_anglePointIds.begin(), g_anglePointIds.end(), pointId) != g_anglePointIds.end()) return;
    g_anglePointIds.push_back(pointId);
    if (g_anglePointIds.size() == 3) {
        AngleMeasurement angle;
        angle.id = g_nextAngleId++;
        angle.pointA = g_anglePointIds[0];
        angle.vertex = g_anglePointIds[1];
        angle.pointC = g_anglePointIds[2];
        g_angles.push_back(angle);
        g_selection = { SelectionKind::Angle, angle.id };
        g_anglePointIds.clear();
        MarkDirty(hWnd);
        SyncProperties(hWnd);
    } else InvalidateRect(hWnd, nullptr, FALSE);
}

void HandleToolbarClick(HWND hWnd, POINT mouse) {
    for (int index = 0; index < 4; ++index) {
        if (Contains(ToolButtonRect(index), mouse)) {
            SetTool(hWnd, static_cast<Tool>(index));
            SyncProperties(hWnd);
            return;
        }
    }
    if (Contains(FileButtonRect(0), mouse)) {
        OpenModel(hWnd);
    } else if (Contains(FileButtonRect(1), mouse)) {
        SaveModel(hWnd, false);
    }
}

void EnableDarkTitleBar(HWND hWnd) {
    HMODULE library = LoadLibraryW(L"dwmapi.dll");
    if (!library) return;
    using DwmSetWindowAttributeProc = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttribute = reinterpret_cast<DwmSetWindowAttributeProc>(GetProcAddress(library, "DwmSetWindowAttribute"));
    if (setAttribute) {
        const BOOL enabled = TRUE;
        if (FAILED(setAttribute(hWnd, 20, &enabled, sizeof(enabled)))) setAttribute(hWnd, 19, &enabled, sizeof(enabled));
    }
    FreeLibrary(library);
}

} // namespace

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    SetProcessDPIAware();
    g_startupFilePath = lpCmdLine ? lpCmdLine : L"";
    const size_t firstCharacter = g_startupFilePath.find_first_not_of(L" \t");
    if (firstCharacter == std::wstring::npos) {
        g_startupFilePath.clear();
    } else {
        g_startupFilePath.erase(0, firstCharacter);
        if (!g_startupFilePath.empty() && g_startupFilePath.front() == L'\"') {
            const size_t closingQuote = g_startupFilePath.find(L'\"', 1);
            g_startupFilePath = g_startupFilePath.substr(1, closingQuote == std::wstring::npos
                ? std::wstring::npos : closingQuote - 1);
        }
    }
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_KINEMATICS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    if (!InitInstance(hInstance, nCmdShow)) return FALSE;
    ACCEL acceleratorEntries[] = {
        { static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('N'), IDM_NEW_MODEL },
        { static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('O'), IDM_OPEN_MODEL },
        { static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('S'), IDM_SAVE_MODEL },
        { static_cast<BYTE>(FVIRTKEY | FCONTROL | FSHIFT), static_cast<WORD>('S'), IDM_SAVE_MODEL_AS },
        { FVIRTKEY, VK_F1, IDM_HELP_CONTENTS }
    };
    HACCEL accelerators = CreateAcceleratorTableW(acceleratorEntries,
        static_cast<int>(sizeof(acceleratorEntries) / sizeof(acceleratorEntries[0])));
    MSG message = {};
    while (GetMessage(&message, nullptr, 0, 0)) {
        if (!TranslateAccelerator(message.hwnd, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);
    return static_cast<int>(message.wParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KINEMATICS));
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(kWindow);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(IDC_KINEMATICS);
    windowClass.lpszClassName = szWindowClass;
    windowClass.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&windowClass);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    hInst = hInstance;
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int availableWidth = workArea.right - workArea.left;
    const int availableHeight = workArea.bottom - workArea.top;
    const int width = (std::min)(1500, availableWidth - 60);
    const int height = (std::min)(1000, availableHeight - 60);
    const int x = workArea.left + (availableWidth - width) / 2;
    const int y = workArea.top + (availableHeight - height) / 2;
    HWND hWnd = CreateWindowExW(WS_EX_COMPOSITED, szWindowClass,
                              L"Kinematics Studio - 2D Linkage Simulator",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, width, height, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    EnableDarkTitleBar(hWnd);
    if (!g_startupFilePath.empty()) LoadModelFromFile(hWnd, g_startupFilePath, true);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateEditorControls(hWnd);
        LayoutControls(hWnd);
        SyncProperties(hWnd);
        HMENU mainMenu = GetMenu(hWnd);
        HMENU fileMenu = mainMenu ? GetSubMenu(mainMenu, 0) : nullptr;
        if (fileMenu) {
            InsertMenuW(fileMenu, 0, MF_BYPOSITION | MF_STRING, IDM_NEW_MODEL, L"&New\tCtrl+N");
            InsertMenuW(fileMenu, 1, MF_BYPOSITION | MF_STRING, IDM_OPEN_MODEL, L"&Open...\tCtrl+O");
            InsertMenuW(fileMenu, 2, MF_BYPOSITION | MF_STRING, IDM_SAVE_MODEL, L"&Save\tCtrl+S");
            InsertMenuW(fileMenu, 3, MF_BYPOSITION | MF_STRING, IDM_SAVE_MODEL_AS, L"Save &As...\tCtrl+Shift+S");
            InsertMenuW(fileMenu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        }
        HMENU helpMenu = mainMenu ? GetSubMenu(mainMenu, 1) : nullptr;
        if (helpMenu) InsertMenuW(helpMenu, 0, MF_BYPOSITION | MF_STRING, IDM_HELP_CONTENTS, L"&Controls\tF1");
        DrawMenuBar(hWnd);
        UpdateWindowTitle(hWnd);
        return 0;
    }
    case WM_SIZE:
        LayoutControls(hWnd);
        InvalidateCanvasAndStatus(hWnd);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 960;
        info->ptMinTrackSize.y = 680;
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        switch (id) {
        case IDM_ABOUT: DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About); return 0;
        case IDM_EXIT: DestroyWindow(hWnd); return 0;
        case IDM_NEW_MODEL:
        case IDC_CLEAR_MODEL: ClearModel(hWnd, true); return 0;
        case IDM_OPEN_MODEL: OpenModel(hWnd); return 0;
        case IDM_SAVE_MODEL: SaveModel(hWnd, false); return 0;
        case IDM_SAVE_MODEL_AS: SaveModel(hWnd, true); return 0;
        case IDM_RESET_VIEW:
        case IDC_RESET_VIEW_BUTTON: ResetView(hWnd); return 0;
        case IDM_HELP_CONTENTS: ShowControlsHelp(hWnd); return 0;
        case IDC_SNAP:
            if (notification == BN_CLICKED && !g_syncingControls) {
                g_snapToGrid = SendMessageW(g_controls.snap, BM_GETCHECK, 0, 0) == BST_CHECKED;
                MarkDirty(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        case IDC_POINT_FIXED:
            if (notification == BN_CLICKED && !g_syncingControls) {
                const bool checked = SendMessageW(g_controls.pointFixed, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_defaultPointFixed = checked;
                if (g_selection.kind == SelectionKind::Point) {
                    if (ModelPoint* point = FindPoint(g_selection.id)) point->fixed = checked;
                }
                MarkDirty(hWnd);
                SyncProperties(hWnd);
            }
            return 0;
        case IDC_LINK_FIXED:
            if (notification == BN_CLICKED && !g_syncingControls) {
                const bool checked = SendMessageW(g_controls.linkFixed, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_defaultLinkFixed = checked;
                if (g_selection.kind == SelectionKind::Link) {
                    if (ModelLink* link = FindLink(g_selection.id)) {
                        link->fixedLength = checked;
                        if (checked) link->targetLength = CurrentLinkLength(*link);
                    }
                }
                MarkDirty(hWnd);
                SyncProperties(hWnd);
            }
            return 0;
        case IDC_APPLY_POINT:
            if (notification == BN_CLICKED) ApplyPointProperties(hWnd);
            return 0;
        case IDC_APPLY_LINK:
            if (notification == BN_CLICKED) ApplyLinkProperties(hWnd);
            return 0;
        case IDC_APPLY_ANGLE:
            if (notification == BN_CLICKED) ApplyAngleProperties(hWnd);
            return 0;
        case IDC_DELETE_SELECTED:
            if (notification == BN_CLICKED) DeleteSelection(hWnd);
            return 0;
        default: break;
        }
        break;
    }
    case WM_DRAWITEM:
        DrawOwnerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_LBUTTONDOWN: {
        SetFocus(hWnd);
        const POINT mouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        g_lastMouse = mouse;
        if (mouse.y < kToolbarHeight) {
            HandleToolbarClick(hWnd, mouse);
            return 0;
        }
        if (!Contains(CanvasRect(hWnd), mouse)) return 0;
        switch (g_tool) {
        case Tool::Select: SelectAt(hWnd, mouse); break;
        case Tool::Point: AddPointAt(hWnd, mouse); break;
        case Tool::Link: UseLinkTool(hWnd, mouse); break;
        case Tool::Angle: UseAngleTool(hWnd, mouse); break;
        }
        return 0;
    }
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        const POINT mouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (Contains(CanvasRect(hWnd), mouse)) {
            g_panning = true;
            g_panMouseStart = mouse;
            g_panStartX = g_panX;
            g_panStartY = g_panY;
            SetCapture(hWnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        g_lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_draggingPoint && (wParam & MK_LBUTTON)) {
            SolveConstraints(g_draggedPointId, SnapPosition(ScreenToWorld(hWnd, g_lastMouse)));
            MarkDirty(hWnd);
        } else if (g_panning && (wParam & (MK_MBUTTON | MK_RBUTTON))) {
            g_panX = g_panStartX + g_lastMouse.x - g_panMouseStart.x;
            g_panY = g_panStartY + g_lastMouse.y - g_panMouseStart.y;
            MarkDirty(hWnd);
        }
        InvalidateCanvasAndStatus(hWnd);
        return 0;
    case WM_LBUTTONUP:
        if (g_draggingPoint) {
            g_draggingPoint = false;
            g_draggedPointId = 0;
            ReleaseCapture();
            SyncProperties(hWnd);
        }
        return 0;
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        if (g_panning) {
            g_panning = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_draggingPoint = false;
        g_draggedPointId = 0;
        g_panning = false;
        return 0;
    case WM_MOUSEWHEEL: {
        POINT mouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &mouse);
        if (!Contains(CanvasRect(hWnd), mouse)) return 0;
        const Vec2 worldAtCursor = ScreenToWorld(hWnd, mouse);
        const double factor = std::pow(1.15, static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
        g_zoom = (std::max)(0.25, (std::min)(8.0, g_zoom * factor));
        const RECT canvas = CanvasRect(hWnd);
        const double centreX = (canvas.left + canvas.right) * 0.5;
        const double centreY = (canvas.top + canvas.bottom) * 0.5;
        g_panX = mouse.x - centreX - worldAtCursor.x * g_zoom;
        g_panY = mouse.y - centreY + worldAtCursor.y * g_zoom;
        MarkDirty(hWnd);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_DELETE: DeleteSelection(hWnd); return 0;
        case VK_F1: ShowControlsHelp(hWnd); return 0;
        case VK_ESCAPE:
            if (g_linkStartPointId || !g_anglePointIds.empty()) ClearPendingSteps();
            else g_selection = {};
            SyncProperties(hWnd);
            return 0;
        case 'V': SetTool(hWnd, Tool::Select); SyncProperties(hWnd); return 0;
        case 'P': SetTool(hWnd, Tool::Point); SyncProperties(hWnd); return 0;
        case 'L': SetTool(hWnd, Tool::Link); SyncProperties(hWnd); return 0;
        case 'A': SetTool(hWnd, Tool::Angle); SyncProperties(hWnd); return 0;
        default: break;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT cursor = {};
            GetCursorPos(&cursor);
            ScreenToClient(hWnd, &cursor);
            if (Contains(CanvasRect(hWnd), cursor)) {
                SetCursor(LoadCursor(nullptr, g_panning ? IDC_SIZEALL : IDC_CROSS));
                return TRUE;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_panelBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, RGB(21, 26, 33));
        return reinterpret_cast<LRESULT>(g_editBrush);
    }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE:
        if (ConfirmDiscardChanges(hWnd)) DestroyWindow(hWnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(hWnd, &paint);
        PaintEditor(hWnd, dc);
        EndPaint(hWnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (g_uiFont) DeleteObject(g_uiFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_smallFont) DeleteObject(g_smallFont);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        if (g_editBrush) DeleteObject(g_editBrush);
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    if (message == WM_INITDIALOG) return TRUE;
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
    }
    return FALSE;
}
