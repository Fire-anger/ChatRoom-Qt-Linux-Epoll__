#include "mymain.h"
#include "ui_mymain.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QListWidgetItem>

/**
 * @brief 聊天室客户端主窗口构造函数
 * @param localIp 本机IP，作为客户端登录用户名
 * @param serverIp 服务端IP地址
 * @param serverPort 服务端端口号
 * @param parent 父窗口指针
 */
MyMain::MyMain(const QString &localIp, const QString &serverIp, quint16 serverPort, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MyMain)
    , m_localName(localIp)    // 保存本机登录用户名(本机IP)
    , m_selectedTarget("all") // 默认聊天目标：all代表公共大群
{
    ui->setupUi(this);
    resize(1100,700);           // 设置窗口初始大小
    setWindowTitle("Qt聊天室客户端");

    // ------------------------------逻辑处理--------------------------------
    // 1.创建TCP通信套接字
    m_socket = new QTcpSocket(this);
    // 连接成功的信号
    connect(m_socket, &QTcpSocket::connected, this, &MyMain::onSocketConnected);
    // 套接字收到数据信号，绑定读数据槽函数   信号不需要实现，槽函数需要实现
    connect(m_socket,&QTcpSocket::readyRead, this, &MyMain::onReadyRead);
    // 连接断开提示
    connect(m_socket, &QTcpSocket::disconnected, this, [this](){
        m_chatDisplay->append("[系统] 与服务端的连接已断开");
    });
    connect(m_socket,&QTcpSocket::errorOccurred,this,&MyMain::onSocketError);   // Qt6版本
    // Qt5版本的socket错误信号绑定（Qt6使用errorOccurred）
    // connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
    //         this, &MyMain::onSocketError);

    // 主动连接聊天室服务端
    m_socket->connectToHost(serverIp, serverPort);

    // ---------- UI布局：分割窗口，左侧用户列表，右侧聊天区域 ----------
    m_splitter = new QSplitter(Qt::Horizontal,this);
    setCentralWidget(m_splitter);

    // 左侧：在线用户列表控件，最大宽度260
    m_userList = new QListWidget;
    m_userList->setMaximumWidth(260);
    // 添加默认大群选项
    new QListWidgetItem("【大群‑全部用户】",m_userList);
    // 用户点击列表项，切换聊天对象（群聊/私聊）
    connect(m_userList,&QListWidget::itemClicked,this,&MyMain::onUserListItemClick);
    m_splitter->addWidget(m_userList);

    // 右侧整体容器
    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);

    // 聊天消息显示框，设置只读，只看不能编辑
    m_chatDisplay = new QTextEdit();
    m_chatDisplay->setReadOnly(true);
    rightLayout->addWidget(m_chatDisplay);

    // 输入框 + 发送按钮横向布局
    QHBoxLayout *inputLayout = new QHBoxLayout();
    m_inputEdit = new QTextEdit();
    m_inputEdit->setMaximumHeight(80); // 限制输入框最大高度
    m_btnSend = new QPushButton("发送");
    // 绑定发送按钮点击事件
    connect(m_btnSend,&QPushButton::clicked,this,&MyMain::onSendMsg);
    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(m_btnSend);
    rightLayout->addLayout(inputLayout);

    m_splitter->addWidget(rightWidget);
    // 设置分隔栏权重，右侧聊天区域自动拉伸占满剩余空间
    m_splitter->setStretchFactor(1,1);

    // 发送登录协议报文，告诉服务端我上线了，用户名为本机IP    就是这一句每次在连接成功之前都会弹出连接错误，是因为TCP还在进行连接
    // sendProtocolMsg(QString("login:%1").arg(m_localName));
}


MyMain::~MyMain()
{
    delete ui;
}

/**
 * @brief 用户点击左侧用户列表，切换聊天目标
 * @param item 被点击的列表项
 */
void MyMain::onUserListItemClick(QListWidgetItem *item)
{
    QString text = item->text();
    if(text.startsWith("【大群"))
    {
        m_selectedTarget = "all";
        m_chatDisplay->append("\n====切换到大群聊天====");
    }
    else
    {
        m_selectedTarget = text;
        m_chatDisplay->append(QString("\n====切换和[%1]私聊====").arg(m_selectedTarget));
    }
}

/**
 * @brief TCP真正建立连接完成，此时状态才是ConnectedState，发送登录协议
 */
void MyMain::onSocketConnected()
{
    sendProtocolMsg(QString("login:%1").arg(m_localName));
}

/**
 * @brief 发送按钮点击槽函数，组装消息并发送协议包
 */
void MyMain::onSendMsg()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if(text.isEmpty()) return;

    QString pkt;
    if(m_selectedTarget == "all")
    {
        pkt = QString("chat:%1").arg(text);
        m_chatDisplay->append(QString("[我](群聊) %1").arg(text));
    }
    else
    {
        pkt = QString("to:%1:%2").arg(m_selectedTarget,text);
        m_chatDisplay->append(QString("[我→%1](私聊) %2").arg(m_selectedTarget,text));
    }
    sendProtocolMsg(pkt);
    m_inputEdit->clear();
}

/**
 * @brief 统一发送协议报文封装函数，每条报文末尾加换行符作为分隔
 * @param msg 协议字符串，如 login:xxx / chat:xxx / to:user:xxx
 */
void MyMain::sendProtocolMsg(const QString &msg)
{
    if(m_socket->state() != QTcpSocket::ConnectedState)
    {
        QMessageBox::warning(this,"错误","服务端未连接！");
        return;
    }
    m_socket->write(QString("%1\n").arg(msg).toUtf8());
}

/**
 * @brief socket收到服务端数据触发：先追加到缓冲区，再按换行符拆出完整报文逐条解析
 *        （解决TCP粘包/半包问题：一次可能收到多条，也可能一条被拆成多次）
 */
void MyMain::onReadyRead()
{
    m_recvBuf.append(m_socket->readAll());
    int idx;
    while((idx = m_recvBuf.indexOf('\n')) != -1)
    {
        QByteArray line = m_recvBuf.left(idx);
        m_recvBuf.remove(0, idx + 1);
        parseServerData(QString::fromUtf8(line));
    }
}

/**
 * @brief 解析服务端返回的一条完整报文
 * 协议：sys:内容 / users:用户1,用户2 / chat:发送者:内容 / to:发送者:内容
 * @param data 去掉换行后的一条报文
 */
void MyMain::parseServerData(const QString &data)
{
    QString line = data;
    if(line.endsWith('\r')) line.chop(1);   // 兼容 Windows 换行 \r\n
    if(line.isEmpty()) return;

    if(line.startsWith("sys:"))
    {
        m_chatDisplay->append(QString("[系统] %1").arg(line.mid(4)));
    }
    else if(line.startsWith("users:"))
    {
        updateUserList(line.mid(6));
    }
    else if(line.startsWith("chat:"))
    {
        // chat:发送者:内容
        QString payload = line.mid(5);
        int colon = payload.indexOf(':');
        QString sender = (colon >= 0) ? payload.left(colon) : QString();
        QString text  = (colon >= 0) ? payload.mid(colon + 1) : payload;
        m_chatDisplay->append(QString("[%1](群聊) %2").arg(sender, text));
    }
    else if(line.startsWith("to:"))
    {
        // to:发送者:内容
        QString payload = line.mid(3);
        int colon = payload.indexOf(':');
        QString sender = (colon >= 0) ? payload.left(colon) : QString();
        QString text  = (colon >= 0) ? payload.mid(colon + 1) : payload;
        m_chatDisplay->append(QString("[%1](私聊) %2").arg(sender, text));
    }
    else
    {
        m_chatDisplay->append(QString("[未知报文] %1").arg(line));
    }
}

/**
 * @brief 根据服务端推送的在线用户列表刷新左侧列表（保留第一项"大群"）
 * @param names 逗号分隔的用户名字符串
 */
void MyMain::updateUserList(const QString &names)
{
    // 先移除除第一项"大群"以外的所有用户项
    while(m_userList->count() > 1)
    {
        QListWidgetItem *item = m_userList->takeItem(1);
        delete item;
    }

    QStringList list = names.split(',', Qt::SkipEmptyParts);
    for(const QString &name : list)
    {
        if(name == m_localName) continue;   // 不显示自己
        new QListWidgetItem(name, m_userList);
    }

    // 如果当前私聊对象已不在线，回退到大群
    if(m_selectedTarget != "all")
    {
        bool found = false;
        for(int i = 0; i < m_userList->count(); i++)
        {
            if(m_userList->item(i)->text() == m_selectedTarget)
            {
                found = true;
                break;
            }
        }
        if(!found)
        {
            m_selectedTarget = "all";
            m_chatDisplay->append("[系统] 私聊对象已下线，已切换回大群");
        }
    }
}

/**
 * @brief socket网络异常槽函数，弹出错误提示框
 * @param err 套接字错误枚举，这里未使用，直接读取errorString获取错误文本
 */
void MyMain::onSocketError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    QMessageBox::critical(this,"网络错误",m_socket->errorString());
}
