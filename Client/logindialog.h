#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H


#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QNetworkInterface>  // 遍历网卡地址需要这个头文件
#include <QHostAddress>
#include <QTimer>  // QTimer定时器类的头文件   Qtime是时间类

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    // 对外提供接口，外部拿到登录参数
    QString getServerIp();
    quint16 getServerPort();
    QString getLocalIp(); //本机IP作为登录用户名

private:
    QLineEdit *m_editIp;
    QLineEdit *m_editPort;
    QString m_localIp;
    QPushButton *m_btnOk;       //登录按钮对象指针
    bool m_isConnectTesting;  // 是否正在进行连接探测

private slots:
    // 按下登录按钮的槽函数
    void onBtnLoginClicked();
};

#endif // LOGINDIALOG_H
