/*
 * Copyright (C) 2014 Red Hat
 *
 * This file is part of qconnect.
 *
 * Qconnect is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
extern "C" {
#include <stdarg.h>
#include <stdio.h>
}
//#include <QtConcurrent/QtConcurrentRun>
#include <QtConcurrentRun>
#include <QMessageBox>
#include <vpninfo.h>
#include <storage.h>
#include <QLineEdit>
#include "logdialog.h"
#include "editdialog.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    const char *version = openconnect_get_version();
    ui->setupUi(this);
    this->setWindowTitle(QLatin1String("Qconnect (openconnect ")+QLatin1String(version)+QLatin1String(")"));

    timer = new QTimer(this);

    connect(timer, SIGNAL(timeout()), this, SLOT(request_update_stats()));
    connect(ui->comboBox->lineEdit(), SIGNAL(returnPressed()), this, SLOT(on_connectBtn_clicked()), Qt::QueuedConnection);

}

MainWindow::~MainWindow()
{
    timer->stop();
    delete ui;
    delete timer;
}

QString
value_to_string(uint64_t bytes)
{
    QString r;
    if (bytes > 1000 && bytes < 1000 * 1000) {
        bytes /= 1000;
        r = QString::number((int)bytes);
        r += " KB";
        return r;
    } else if (bytes >= 1000 * 1000 && bytes < 1000 * 1000 * 1000) {
        bytes /= 1000*1000;
        r = QString::number((int)bytes);
        r += " MB";
        return r;
    } else if (bytes >= 1000 * 1000 * 1000) {
        bytes /= 1000*1000*1000;
        r = QString::number((int)bytes);
        r += " GB";
        return r;
    } else {
        r = QString::number((int)bytes);
        r += " bytes";
        return r;
    }
}
void MainWindow::updateStats(const struct oc_stats *stats)
{
    ui->lcdDown->setText(value_to_string(stats->tx_bytes));
    ui->lcdUp->setText(value_to_string(stats->rx_bytes));
}

void MainWindow::reload_settings()
{
    QStringList servers;
    ui->comboBox->clear();

    servers = get_server_list(this->settings);

    for (int i = 0;i<servers.size();i++) {
        ui->comboBox->addItem(servers.at(i));
    }
}

void MainWindow::set_settings(QSettings *s)
{
    this->settings = s;
    reload_settings();
};

void MainWindow::updateProgressBar(QString str)
{
    QMutexLocker locker(&this->progress_mutex);
//    ui->statusBar->showMessage(str, 20);
    if (str.isEmpty() == false)
        log.append(str);
}

void MainWindow::enableDisconnect(bool val)
{
    if (val == true) {
        ui->disconnectBtn->setEnabled(true);
        ui->connectBtn->setEnabled(false);
    } else {
        QPixmap pixmap(OFF_ICON);
        timer->stop();
        ui->IPLabel->setText("");
        ui->DNSLabel->setText("");
        ui->IP6Label->setText("");

        ui->disconnectBtn->setEnabled(false);
        ui->connectBtn->setEnabled(true);
        ui->iconLabel->setPixmap(pixmap);
    }
}

static void main_loop(VpnInfo *vpninfo, MainWindow *m)
{
    vpninfo->mainloop();
    m->updateProgressBar(vpninfo->last_err);
    m->enableDisconnect(false);

    m->stop_timer();

    delete vpninfo;
    vpninfo = NULL;
}

void MainWindow::on_disconnectBtn_clicked()
{
    this->vpninfo->disconnect();
}

void MainWindow::on_connectBtn_clicked()
{
    VpnInfo *vpninfo = NULL;
    StoredServer *ss = new StoredServer(this->settings);
    QFuture<void> result;
    QString name;
    int ret;
    QPixmap pixmap(ON_ICON);
    QString ip, ip6, dns;

    if (this->vpninfo != NULL)
        return;

    if (ui->comboBox->currentText().isEmpty()) {
        QMessageBox::information(
            this,
            tr(APP_NAME),
            tr("You need to specify a gateway. E.g. vpn.example.com:443") );
        return;
    }

    timer->start(UPDATE_TIMER);

    name = ui->comboBox->currentText();
    if (ss->load(name) == 0) {
        ss->set_servername(name);
    }

    vpninfo = new VpnInfo(APP_STRING, ss, this);
    if (vpninfo == NULL)
        return;

    vpninfo->parse_url(ss->get_servername().toLocal8Bit().data());

    enableDisconnect(true);

    /* XXX openconnect_set_http_proxy */

    ret = vpninfo->connect();
    if (ret != 0) {
        updateProgressBar(vpninfo->last_err);
        goto fail;
    }

    ret = vpninfo->dtls_connect();
    if (ret != 0) {
        updateProgressBar(vpninfo->last_err);
        goto fail;
    }

    updateProgressBar("saving peer's information");
    vpninfo->ss->save();

    ui->iconLabel->setPixmap(pixmap);

    vpninfo->get_info(dns, ip, ip6);;
    ui->IPLabel->setText(ip);
    ui->IP6Label->setText(ip6);

    ui->DNSLabel->setText(dns);

    result = QtConcurrent::run (main_loop, vpninfo, this);
    this->vpninfo = vpninfo;

    return;
 fail:
    delete vpninfo;
    vpninfo = NULL;
    enableDisconnect(false);
    return;
}


void MainWindow::on_toolButton_clicked()
{
    EditDialog dialog(ui->comboBox->currentText(), this->settings);

    dialog.exec();
    reload_settings();
}

void MainWindow::on_toolButton_2_clicked()
{
    if (ui->comboBox->currentText().isEmpty() == false) {
        QMessageBox mbox;
        int ret;

        mbox.setText(QLatin1String("Are you sure you want to remove ")+ui->comboBox->currentText()+"?");
        mbox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
        mbox.setDefaultButton(QMessageBox::Cancel);
        ret = mbox.exec();
        if (ret == QMessageBox::Ok) {
            remove_server(settings, ui->comboBox->currentText());
            reload_settings();
        }
    }
}

void MainWindow::on_toolButton_3_clicked()
{
    LogDialog dialog(this->log);

    dialog.exec();
}

void MainWindow::request_update_stats()
{
    if (vpninfo) {
        vpninfo->request_update_stats();
    }
}

void MainWindow::stop_timer()
{
    timer->stop();
}