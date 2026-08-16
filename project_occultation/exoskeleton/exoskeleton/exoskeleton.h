

#ifndef OCCULTATION_EXOSKELETON_EXOSKELETON_H_
#define OCCULTATION_EXOSKELETON_EXOSKELETON_H_

#include <QObject>
#include <QRect>
#include <QString>

class QSocketNotifier;

typedef struct _kywc_context kywc_context;
typedef struct _kywc_output kywc_output;
typedef struct _kywc_toplevel kywc_toplevel;
typedef struct _kywc_workspace kywc_workspace;

namespace Occultation {

class SwitcherModel;

class Exoskeleton final : public QObject {
  Q_OBJECT
  Q_PROPERTY(SwitcherModel *model READ model CONSTANT)
  Q_PROPERTY(bool started READ isStarted NOTIFY startedChanged)
  Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(QRect screenGeometry READ screenGeometry WRITE setScreenGeometry
                 NOTIFY screenGeometryChanged)
  Q_PROPERTY(bool allDesktops READ allDesktops WRITE setAllDesktops NOTIFY
                 allDesktopsChanged)
  Q_PROPERTY(bool noModifierGrab READ noModifierGrab WRITE setNoModifierGrab
                 NOTIFY noModifierGrabChanged)

 public:
  static Exoskeleton *instance();

  SwitcherModel *model() const;
  kywc_context *context() const;

  bool isStarted() const;
  bool isVisible() const;
  int currentIndex() const;
  QRect screenGeometry() const;
  bool allDesktops() const;
  bool noModifierGrab() const;

  Q_INVOKABLE bool start(const QString &displayName = QString());
  Q_INVOKABLE void stop();
  Q_INVOKABLE void show(int direction = 1);
  Q_INVOKABLE void accept();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void next();
  Q_INVOKABLE void previous();

  void setCurrentIndex(int index);
  void setScreenGeometry(const QRect &geometry);
  void setAllDesktops(bool allDesktops);
  void setNoModifierGrab(bool noModifierGrab);

  QString protocolUuid(const QUuid &windowId) const;
  bool isDesktopId(const QUuid &windowId) const;
  QString currentOutputUuid() const;
  QString currentWorkspaceUuid() const;

  static void handleContextCreate(kywc_context *context, void *data);
  static void handleContextDestroy(kywc_context *context, void *data);
  static void handleNewOutput(kywc_context *context, kywc_output *output,
                              void *data);
  static void handleNewToplevel(kywc_context *context, kywc_toplevel *toplevel,
                                void *data);
  static void handleNewWorkspace(kywc_context *context,
                                 kywc_workspace *workspace, void *data);

 Q_SIGNALS:
  void startedChanged();
  void visibleChanged();
  void currentIndexChanged(int index);
  void screenGeometryChanged();
  void allDesktopsChanged();
  void noModifierGrabChanged();
  void connectionLost();
  void showDesktopRequested();

 private Q_SLOTS:
  void processWaylandEvents();
  void requestShowDesktop();

 private:
  explicit Exoskeleton(QObject *parent = nullptr);
  ~Exoskeleton() override;

  void setVisible(bool visible);
  void normalizeCurrentIndex();

  SwitcherModel *m_model = nullptr;
  kywc_context *m_context = nullptr;
  QSocketNotifier *m_notifier = nullptr;
  QRect m_screenGeometry;
  bool m_visible = false;
  bool m_allDesktops = false;
  bool m_noModifierGrab = false;
  bool m_stopping = false;
  int m_currentIndex = 0;
};

}  // namespace Occultation

#endif
