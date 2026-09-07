#ifndef MYMAIN_H
#define MYMAIN_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QSplitter>

QT_BEGIN_NAMESPACE
namespace Ui { class MyMain; }
QT_END_NAMESPACE

class MyMain : public QMainWindow
{
    Q_OBJECT
public:
    explicit MyMain(const QString &localIp, const QString &serverIp, quint16 serverPort, QWidget *parent = nullptr);
    ~MyMain();

// 槽函数
private slots:
    void onReadyRead();
    void onSendMsg();
    void onSocketError(QAbstractSocket::SocketError err);
    void onUserListItemClick(QListWidgetItem *item);
    void onSocketConnected();   //socket连接成功触发

private:
    Ui::MyMain *ui;

    QTcpSocket *m_socket;
    QString m_localName;         //本机登录名称（本机IP）
    QSplitter *m_splitter;       //分割窗口：左边列表，右边聊天区
    QListWidget *m_userList;     //左侧在线用户列表
    QTextEdit *m_chatDisplay;    //聊天消息展示
    QTextEdit *m_inputEdit;      //消息输入框
    QPushButton *m_btnSend;
    QString m_selectedTarget;    //"all"代表大群公聊
    QByteArray m_recvBuf;        //接收缓冲区，处理TCP粘包/半包

    void sendProtocolMsg(const QString &msg);
    void parseServerData(const QString &data);
    void updateUserList(const QString &names);
};

#endif // MYMAIN_H
