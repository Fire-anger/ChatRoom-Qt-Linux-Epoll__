#include "logindialog.h"
#include <QVBoxLayout>
#include <QHostAddress>
#include <QTcpSocket>
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    // 设置对话框窗口标题
    setWindowTitle("连接聊天室服务端");
    // 创建垂直布局管理器，依附到当前对话框
    QVBoxLayout *layout = new QVBoxLayout(this);

    // 服务器IP输入标签+输入框，预设默认服务器IP
    layout->addWidget(new QLabel("服务端IP:"));
    m_editIp = new QLineEdit("192.168.88.137"); //填你的Ubuntu虚拟机IP
    layout->addWidget(m_editIp);

    // 服务器端口标签+输入框，预设端口为6666
    layout->addWidget(new QLabel("服务端端口:"));
    m_editPort = new QLineEdit("6666");
    layout->addWidget(m_editPort);

    //自动获取本机网卡IP（排除127.0.0.1）
    m_localIp = "";
    // 方式一：直接将第一个IPv4作为本机的IP地址，有可能获取到虚拟地址
//    for(auto &addr : QNetworkInterface::allAddresses())
//    {
//        // 获取第一个IPv4地址，有可能获取到虚拟地址
//        if(addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost)
//        {
//            m_localIp = addr.toString();   // 获取IP字符串
//            break;
//        }
//        qDebug() << addr;
//        qDebug() << addr.protocol();
//    }

    // 方式二：自动获取本机真实网卡IPv4，跳过VirtualBox、VMware虚拟网卡网段（仅限我的电脑）
    QList<QHostAddress> addrList = QNetworkInterface::allAddresses();
    for (auto &addr : addrList)
    {
        // 只保留IPv4，排除本地回环地址127.0.0.1
        if(addr.protocol() != QAbstractSocket::IPv4Protocol || addr == QHostAddress::LocalHost)
        {
            continue;
        }
        QString ipStr = addr.toString();
        // 过滤本机虚拟网卡网段 VirtualBox + VMware
        if(ipStr.startsWith("192.168.56.") || ipStr.startsWith("192.168.189.") || ipStr.startsWith("192.168.88."))
        {
            continue;
        }
        // 拿到真实IP
        m_localIp = ipStr;
        break;
    }

    // 方式三：遍历所有网卡对象   Qt5.15 版本才新增这个值 IsVirtual，因此只能通过手动过滤的方式来解决
//    for(const QNetworkInterface &iface : QNetworkInterface::allInterfaces())
//    {
//        qDebug() << iface;
//        //跳过没有启用、未运行的网卡
//        if(!iface.flags().testFlag(QNetworkInterface::IsUp) || !iface.flags().testFlag(QNetworkInterface::IsRunning))
//            continue;
//        //跳过虚拟网卡、回环网卡
//        if(iface.flags().testFlag(QNetworkInterface::IsLoopBack) || iface.flags().testFlag(QNetworkInterface::IsVirtual))
//            continue;

//        //遍历该网卡的地址列表
//        for(const QNetworkAddressEntry &entry : iface.addressEntries())
//        {
//            QHostAddress addr = entry.ip();
//            if(addr.protocol() == QAbstractSocket::IPv4Protocol)
//            {
//                m_localIp = addr.toString();
//                goto findIpDone; //找到直接跳出双重循环
//            }
//        }
//    }
//    findIpDone:

    // 初始化是否正在进行连接探测
    m_isConnectTesting = false;

    // 显示提示标签：展示本机作为登录用户名使用的IP
    layout->addWidget(new QLabel(QString("本机登录用户名(本机IP):%1").arg(m_localIp)));

    // 创建登录按钮，添加到布局
    m_btnOk = new QPushButton("登录连接");
    layout->addWidget(m_btnOk);
    // 按钮点击，触发对话框accept，返回QDialog:Accepted
    connect(m_btnOk, &QPushButton::clicked, this, &LoginDialog::onBtnLoginClicked);
}

// 返回服务端IP
QString LoginDialog::getServerIp()
{
    return m_editIp->text();
}
// 返回服务端端口
quint16 LoginDialog::getServerPort()
{
    return m_editPort->text().toUShort();
}
// 返回本地IP
QString LoginDialog::getLocalIp()
{
    return m_localIp;
}

// 按下登录按钮的槽函数
/**
 * @brief 登录按钮点击槽函数：探测TCP连接，不直接关闭对话框
 */
void LoginDialog::onBtnLoginClicked()
{
    // 如果正在探测，直接返回，禁止重复点击
    if(m_isConnectTesting)
    {
        return;
    }

    QString serverIP = m_editIp->text().trimmed();
    quint16 port = m_editPort->text().toUShort();

    if(serverIP.isEmpty() || port == 0)
    {
        QMessageBox::warning(this, "输入错误", "服务端IP或端口不能为空！");
        return;
    }

    m_isConnectTesting = true;
    m_btnOk->setText("正在连接...");
    m_btnOk->setEnabled(false);  // 禁用登录按钮，防止重复点击

    // 临时socket用于探测连通性，this作为父对象自动回收
    QTcpSocket *testSocket = new QTcpSocket(this);
    // 每次探测新建局部定时器，parent绑定testSocket，socket销毁定时器自动销毁
//    QTimer* testTimer = new QTimer(testSocket);
//    testTimer->setSingleShot(true);

    // 连接成功信号
    connect(testSocket, &QTcpSocket::connected, this, [=](){
//        testTimer->stop();     // 停止超时计时器
        testSocket->deleteLater();  // 探测完成销毁socket
        m_isConnectTesting = false;
        m_btnOk->setText("登录连接");
        m_btnOk->setEnabled(true);  // 恢复按钮
        // 连接正常，关闭对话框，返回Accepted
        this->accept();
    });

    // 连接失败信号
    // connect(testSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error), this, [=](QAbstractSocket::SocketError){
    connect(testSocket, &QAbstractSocket::errorOccurred, this, [=](QAbstractSocket::SocketError){
//        testTimer->stop();     // 停止超时计时器
        QString errMsg = testSocket->errorString();
        testSocket->deleteLater();
        m_isConnectTesting = false;
        m_btnOk->setText("登录连接");
        m_btnOk->setEnabled(true);  // 恢复按钮
        QMessageBox::critical(this, "连接失败", QString("无法连接服务端：%1").arg(errMsg));
        // 不调用accept 对话框保持打开，用户可以修改IP端口
    });

    //=====自定义超时3s，3秒连不上直接手动断开====
//    connect(testTimer, &QTimer::timeout, this, [=]{
//        // 只有处于正在连接状态的才abort
//        if(testSocket->state() == QTcpSocket::HostLookupState || testSocket->state() == QTcpSocket::ConnectingState)
//        {
//            testSocket->abort();  // 强制终止正在进行的连接尝试
//        }
//    });

    // 发起异步连接
    testSocket->connectToHost(serverIP, port);
//    testTimer->start(3000);   // 设置3s超时
}



