#include "mymain.h"

#include <QApplication>
#include "logindialog.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LoginDialog dlg;
    if(dlg.exec() != QDialog::Accepted)
    {
        return 0;
    }

    MyMain w(dlg.getLocalIp(), dlg.getServerIp(), dlg.getServerPort());
    w.show();
    return a.exec();
}
