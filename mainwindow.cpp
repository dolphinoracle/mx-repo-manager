/**********************************************************************
 *  mainwindow.cpp
 **********************************************************************
 * Copyright (C) 2015 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-repo-manager.
 *
 * mx-repo-manager is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-repo-manager is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-repo-manager.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QDebug>
#include <QDesktopWidget>
#include <QDir>
#include <QMetaEnum>
#include <QNetworkReply>
#include <QProgressBar>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextEdit>

#include "about.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    qDebug().noquote() << qApp->applicationName() << "version:" << qApp->applicationVersion();
    ui->setupUi(this);
    setConnections();
    setWindowFlags(Qt::Window);
    if (ui->pushOk->icon().isNull())
        ui->pushOk->setIcon(QIcon::fromTheme("dialog-ok", QIcon(":/icons/dialog-ok.svg")));

    if (ui->pushFastestMX->icon().isNull())
        ui->pushFastestMX->setIcon(QIcon::fromTheme("cursor-arrow", QIcon(":/icons/cursor-arrow.svg")));

    if (ui->pushFastestDebian->icon().isNull())
        ui->pushFastestDebian->setIcon(QIcon::fromTheme("cursor-arrow", QIcon(":/icons/cursor-arrow.svg")));

    shell = new Cmd(this);

    connect(shell, &Cmd::started, this, &MainWindow::procStart);
    connect(shell, &Cmd::finished, this, &MainWindow::procDone);

    setProgressBar();

    ui->pushOk->setDisabled(true);

    this->setWindowTitle(tr("MX Repo Manager"));
    ui->tabWidget->setCurrentWidget(ui->tabMX);
    refresh();

    QSize size = this->size();
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
        if (this->isMaximized()) { // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    }

}

MainWindow::~MainWindow()
{
    delete ui;
}

// refresh repo info
void MainWindow::refresh()
{
    getCurrentRepo();
    displayMXRepos(readMXRepos(), QString());
    displayAllRepos(listAptFiles());
    ui->lineSearch->clear();
    ui->lineSearch->setFocus();
}

// replace default Debian repos
void MainWindow::replaceDebianRepos(const QString &url)
{
    // Debian list files that are present by default in MX
    QStringList files {"/etc/apt/sources.list.d/debian.list", "/etc/apt/sources.list.d/debian-stable-updates.list"};

    // make backup folder
    if (!QFileInfo::exists("/etc/apt/sources.list.d/backups"))
        QDir().mkdir("/etc/apt/sources.list.d/backups");

    QString cmd;
    for (const QString &file : files) {
        QFileInfo fileinfo(file);

        // backup file
        cmd = "cp " + file + " /etc/apt/sources.list.d/backups/" + fileinfo.fileName() + ".$(date +%s)";
        shell->run(cmd);

        cmd = "sed -i 's;deb\\s.*/debian/*[^-];deb " + url + " ;' " + file ; // replace deb lines in file
        shell->run(cmd);
        cmd = "sed -i 's;deb-src\\s.*/debian/*[^-];deb-src " + url + ";' " + file; // replace deb-src lines in file
        shell->run(cmd);
        if (url == "https://deb.debian.org/debian/") {
            cmd = "sed -i 's;deb\\s*http://security.debian.org/;deb https://deb.debian.org/debian-security/;' " + file; // replace security.debian.org in file
            shell->run(cmd);
        }
    }
    QMessageBox::information(this, tr("Success"), tr("Your new selection will take effect the next time sources are updated."));
}

// List available repos
QStringList MainWindow::readMXRepos()
{
    QFile file("/usr/share/mx-repo-list/repos.txt");
    if (!file.open(QIODevice::ReadOnly))
        qDebug() << "Count not open file: " << file.fileName();

    QString file_content = file.readAll().trimmed();
    file.close();

    QStringList file_content_list = file_content.split("\n");
    file_content_list.sort();

    // remove commented out lines
    QStringList repos;
    for (const QString &line : file_content_list)
        if (!line.startsWith("#"))
            repos << line;

    extractUrls(repos);
    this->repos = repos;
    return repos;
}

// List current repo
void MainWindow::getCurrentRepo()
{
    current_repo = shell->getCmdOut("grep -m1 '^deb.*/repo/ ' /etc/apt/sources.list.d/mx.list |cut -d' ' -f2 |cut -d/ -f3");
}

int MainWindow::getDebianVerNum()
{
    const QString out = shell->getCmdOut("cat /etc/debian_version");
    QStringList list = out.split(".");
    bool ok;
    int ver = list.at(0).toInt(&ok);
    if (ok)
      return ver;
    else if (list.at(0).split("/").at(0) == "bullseye")
      return 11;
    else if (list.at(0).split("/").at(0) == "bookworm")
      return 12;
    else
      return 0; // unknown
}

QString MainWindow::getDebianVerName(int ver)
{
    switch (ver)
    {
    case 8:  return "jessie";
    case 9:  return "stretch";
    case 10: return "buster";
    case 11: return "bullseye";
    case 12: return "bookworm";
    default:
        qDebug() << "Could not detect Debian version";
        exit(EXIT_FAILURE);
    }
}

// display available repos
void MainWindow::displayMXRepos(const QStringList &repos, const QString &filter)
{
    ui->listWidget->clear();
    for (const QString &repo : repos) {
        if (!filter.isEmpty() && !repo.contains(filter, Qt::CaseInsensitive))
            continue;
        QString country = repo.section("-", 0, 0).trimmed().section(",", 0, 0);
        QListWidgetItem *item = new QListWidgetItem(ui->listWidget);
        QRadioButton *radio = new QRadioButton(repo);
        radio->setIcon(getFlag(country));
        ui->listWidget->setItemWidget(item, radio);
        if (repo.contains(current_repo)) {
            radio->setChecked(true);
            ui->listWidget->scrollToItem(item);
        }
        connect(radio, &QRadioButton::clicked, ui->pushOk, &QPushButton::setEnabled);
    }
}

void MainWindow::displayAllRepos(const QFileInfoList &apt_files)
{
    ui->treeWidget->clear();
    ui->treeWidgetDeb->clear();
    ui->treeWidget->blockSignals(true);
    ui->treeWidgetDeb->blockSignals(true);

    const QStringList columnNames {tr("Lists"), tr("Sources (checked sources are enabled)")};
    ui->treeWidget->setHeaderLabels(columnNames);
    ui->treeWidgetDeb->setHeaderLabels(columnNames);

    QTreeWidgetItem *topLevelItem;
    QTreeWidgetItem *topLevelItemDeb;

    for (const QFileInfo &file_info : apt_files) {
        QString file_name = file_info.fileName();
        QString file = file_info.absoluteFilePath();
        topLevelItem = new QTreeWidgetItem;
        topLevelItem->setText(0, file_name);
        topLevelItemDeb = new QTreeWidgetItem;
        topLevelItemDeb->setText(0, file_name);
        ui->treeWidget->addTopLevelItem(topLevelItem);
        if (file_name.contains("debian"))
            ui->treeWidgetDeb->addTopLevelItem(topLevelItemDeb);
        // topLevelItem look
        topLevelItem->setForeground(0, QBrush(Qt::darkGreen));
        topLevelItemDeb->setForeground(0, QBrush(Qt::darkGreen));
        // load file entries
        const QStringList entries = loadAptFile(file);

        for (const QString &item : entries) {
            // add entries as childItem to treeWidget
            QTreeWidgetItem *childItem = new QTreeWidgetItem(topLevelItem);
            QTreeWidgetItem *childItemDeb = new QTreeWidgetItem(topLevelItemDeb);
            childItem->setText(1, item);
            childItemDeb->setText(1, item);
            // add checkboxes
            childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
            childItemDeb->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
            if (item.startsWith("#")) {
                childItem->setCheckState(1, Qt::Unchecked);
                childItemDeb->setCheckState(1, Qt::Unchecked);
            } else {
                childItem->setCheckState(1, Qt::Checked);
                childItemDeb->setCheckState(1, Qt::Checked);
            }
        }
    }
    for (int i = 0; i < ui->treeWidget->columnCount(); i++)
        ui->treeWidget->resizeColumnToContents(i);

    for (int i = 0; i < ui->treeWidgetDeb->columnCount(); i++)
        ui->treeWidgetDeb->resizeColumnToContents(i);

    ui->treeWidget->expandAll();
    ui->treeWidgetDeb->expandAll();
    ui->treeWidget->blockSignals(false);
    ui->treeWidgetDeb->blockSignals(false);
}

QStringList MainWindow::loadAptFile(const QString &file)
{
    QString entries = shell->getCmdOut("grep '^#*[ ]*deb' " + file);
    return entries.split("\n");
}

void MainWindow::cancelOperation()
{
    shell->halt();
    procDone();
}

void MainWindow::centerWindow()
{
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    int x = (screenGeometry.width()-this->width()) / 2;
    int y = (screenGeometry.height()-this->height()) / 2;
    this->move(x, y);
}

void MainWindow::closeEvent(QCloseEvent *)
{
    settings.setValue("geometry", saveGeometry());
}


// displays the current repo by selecting the item
void MainWindow::displaySelected(const QString &repo)
{
    for (int row = 0; row < ui->listWidget->count(); ++row) {
        QRadioButton *radio = static_cast<QRadioButton*>(ui->listWidget->itemWidget(ui->listWidget->item(row)));
        if (radio->text().contains(repo)) {
            radio->setChecked(true);
            ui->listWidget->scrollToItem(ui->listWidget->item(row));
        }
    }
}

// extract the URLs from the list of repos that contain country names and description
void MainWindow::extractUrls(const QStringList &repos)
{
    QStringList linelist;
    for (const QString &line : repos) {
        linelist = line.split("-");
        linelist.removeAt(0);
        listMXurls += linelist.join("-").trimmed() + " "; // rejoin any repos that contain "-"
    }
}

// set the selected repo
void MainWindow::setSelected()
{
    QString url;
    for (int row = 0; row < ui->listWidget->count(); ++row) {
        QRadioButton *radio = static_cast<QRadioButton*>(ui->listWidget->itemWidget(ui->listWidget->item(row)));
        if (radio->isChecked()) {
            url = radio->text().section(" - ", 1, 1).trimmed();
            replaceRepos(url);
        }
    }
}

void MainWindow::procTime()
{
    bar->setValue((bar->value() + 1) % 100);
//    qApp->processEvents();
}

void MainWindow::procStart()
{
    bar->setValue(0);
    QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    timer.start(100);
    connect(&timer, &QTimer::timeout, this, &MainWindow::procTime);
}

void MainWindow::procDone()
{
    bar->setValue(100);
    timer.stop();
    timer.disconnect();
    QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
}



// replaces the lines in the APT file
void MainWindow::replaceRepos(const QString &url)
{
    QString cmd_mx;
    QString cmd_antix;
    QString repo_line_antix;

    // get Debian version
    const int ver_num = getDebianVerNum();
    const QString ver_name = getDebianVerName(ver_num);

    // mx source files to be edited (mx.list and mx16.list for MX15/16)
    QString mx_file {"/etc/apt/sources.list.d/mx.list"};
    if (QFileInfo::exists("/etc/apt/sources.list.d/mx16.list"))
        mx_file += " /etc/apt/sources.list.d/mx16.list";       // add mx16.list to the list if it exists

    // for MX repos
    QString repo_line_mx = "deb " + url + "/mx/repo/ ";
    QString test_line_mx = "deb " + url + "/mx/testrepo/ ";
    cmd_mx = QString("sed -i 's;deb.*/repo/ ;%1;' %2 && ").arg(repo_line_mx, mx_file) +
            QString("sed -i 's;deb.*/testrepo/ ;%1;' %2").arg(test_line_mx, mx_file);

    if (ver_num < 9 && QFileInfo::exists("/etc/antix-version")) { // Added antix-version check in case running this on a MXfyied Debian
        // for antiX repos
        QString antix_file = "/etc/apt/sources.list.d/antix.list";
        repo_line_antix = (url == "http://mxrepo.com") ? "http://la.mxrepo.com/antix/" + ver_name + "/"
                                                       : url + "/antix/" + ver_name + "/";
        cmd_antix = QString("sed -i 's;https\\?://.*/" + ver_name + "/\\?;%1;' %2").arg(repo_line_antix, antix_file);
    }

    // check if both replacement were successful
    if (shell->run(cmd_mx) && (ver_num >= 9 || shell->run(cmd_antix)))
        QMessageBox::information(this, tr("Success"), tr("Your new selection will take effect the next time sources are updated."));
    else
        QMessageBox::critical(this, tr("Error"), tr("Could not change the repo."));

}

void MainWindow::setConnections()
{
    connect(ui->lineSearch, &QLineEdit::textChanged, this, &MainWindow::lineSearch_textChanged);
    connect(ui->pb_restoreSources, &QPushButton::clicked, this, &MainWindow::pb_restoreSources_clicked);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushFastestDebian, &QPushButton::clicked, this, &MainWindow::pushFastestDebian_clicked);
    connect(ui->pushFastestMX, &QPushButton::clicked, this, &MainWindow::pushFastestMX_clicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushOk, &QPushButton::clicked, this, &MainWindow::pushOk_clicked);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::tabWidget_currentChanged);
    connect(ui->treeWidgetDeb, &QTreeWidget::itemChanged, this, &MainWindow::treeWidgetDeb_itemChanged);
    connect(ui->treeWidget, &QTreeWidget::itemChanged, this, &MainWindow::treeWidget_itemChanged);
}

void MainWindow::setProgressBar()
{
    progress = new QProgressDialog(this);
    bar = new QProgressBar(progress);
    bar->setMaximum(100);
    progCancel = new QPushButton(tr("Cancel"));
    connect(progCancel, &QPushButton::clicked, this, &MainWindow::cancelOperation);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint
                             | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    progress->setCancelButton(progCancel);
    progress->setLabelText(tr("Please wait..."));
    progress->setAutoClose(false);
    progress->setBar(bar);
    bar->setTextVisible(false);
    progress->reset();
}

QFileInfoList MainWindow::listAptFiles()
{
    const QDir apt_dir("/etc/apt/sources.list.d");
    QFileInfoList list {apt_dir.entryInfoList(QStringList("*.list"))};
    const QFile file("/etc/apt/sources.list");
    if (file.size() != 0 && shell->run("grep '^#*[ ]*deb' " + file.fileName(), true))
        list << file;
    return list;
}

// Submit button clicked
void MainWindow::pushOk_clicked()
{
    if (queued_changes.size() > 0) {
        for (const QStringList &changes : qAsConst(queued_changes)) {
            QString text, new_text, file_name;
            text = QRegularExpression::escape(changes.at(0));
            new_text = changes.at(1);
            file_name = changes.at(2);
            QString cmd = QString("sed -i 's;%1;%2;g' %3").arg(text, new_text, file_name);
            shell->run(cmd);
        }
        queued_changes.clear();
    }
    setSelected();
    refresh();
}

// About button clicked
void MainWindow::pushAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(tr("About %1").arg(this->windowTitle()), "<p align=\"center\"><b><h2>" + this->windowTitle()
                       + "</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + qApp->applicationVersion() + "</p><p align=\"center\"><h3>" +
                       tr("Program for choosing the default APT repository") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>",
                       "/usr/share/doc/mx-repo-manager/license.html", tr("%1 License").arg(this->windowTitle()), true);
    this->show();
}

// Help button clicked
void MainWindow::pushHelp_clicked()
{
    QLocale locale;
    QString lang = locale.bcp47Name();
    QString url = "/usr/share/doc/mx-repo-manager/mx-repo-manager.html";

    if (lang.startsWith("fr"))
        url = "https://mxlinux.org/wiki/help-files/help-mx-gestionnaire-de-d%C3%A9p%C3%B4ts";

    displayDoc(url, tr("%1 Help").arg(this->windowTitle()), true);
}

void MainWindow::treeWidget_itemChanged(QTreeWidgetItem * item, int column)
{
    ui->pushOk->setEnabled(true);
    ui->treeWidget->blockSignals(true);
    if (item->text(column).contains("/mx/testrepo/") && item->checkState(column) == Qt::Checked)
        QMessageBox::warning(this, tr("Warning"),
                             tr("You have selected MX Test Repo. It's not recommended to leave it enabled or to upgrade all the packages from it.") +"\n\n" +
                             tr("A safer option is to install packages individually with MX Package Installer."));

    QFile file;
    QString new_text;
    const QString file_name {item->parent()->text(0)};
    const QString text {item->text(column)};
    QStringList changes;
    if (file_name == "sources.list")
        file.setFileName("/etc/apt/" + file_name);
    else
        file.setFileName("/etc/apt/sources.list.d/" + file_name);

    if (item->checkState(column) == Qt::Checked) {
        new_text = text;
        new_text.remove(QRegularExpression("#\\s*"));
        item->setText(column, new_text);
    } else {
        new_text = "# " + text;
        item->setText(column, new_text);
    }
    changes << text << new_text << file.fileName();
    queued_changes << changes;
    ui->treeWidget->blockSignals(false);
}

void MainWindow::treeWidgetDeb_itemChanged(QTreeWidgetItem *item, int column)
{
    ui->pushOk->setEnabled(true);
    ui->treeWidgetDeb->blockSignals(true);
    QFile file;
    QString new_text;
    const QString file_name {item->parent()->text(0)};
    const QString text {item->text(column)};
    QStringList changes;
    if (file_name == "sources.list")
        file.setFileName("/etc/apt/" + file_name);
    else
        file.setFileName("/etc/apt/sources.list.d/" + file_name);

    if (item->checkState(column) == Qt::Checked) {
        new_text = text;
        new_text.remove(QRegularExpression("#\\s*"));
        item->setText(column, new_text);
    } else {
        new_text = "# " + text;
        item->setText(column, new_text);
    }
    changes << text << new_text << file.fileName();
    queued_changes << changes;
    ui->treeWidgetDeb->blockSignals(false);
}

void MainWindow::tabWidget_currentChanged()
{
    if (ui->tabWidget->currentWidget() == ui->tabMX)
        ui->label->setText(tr("Select the APT repository that you want to use:"));
    else
        ui->label->setText(tr("Select the APT repository and sources that you want to use:"));
}

// Transform "country" name to 2-3 letter ISO 3166 country code and provide the QIcon for it
QIcon MainWindow::getFlag(QString country)
{
    QMetaObject metaObject = QLocale::staticMetaObject;
    QMetaEnum metaEnum = metaObject.enumerator(metaObject.indexOfEnumerator("Country"));
    // fix flag of the Netherlands: QLocale::Netherlands
    if (country == QLatin1String("The Netherlands"))
        country = QStringLiteral("Netherlands");
    // fix flag of the United States of America: QLocale::UnitedStates
    if (country == QLatin1String("USA"))
        country = QStringLiteral("UnitedStates");
    if (country == QLatin1String("Anycast") || country == QLatin1String("Any") || country == QLatin1String("World"))
        return QIcon("/usr/share/flags-common/any.png");

    // QMetaEnum metaEnum = QMetaEnum::fromType<QLocale::Country>(); -- not in older Qt versions
    int index = metaEnum.keyToValue(country.remove(" ").toUtf8());
    QList<QLocale> locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::Country(index));
    // qDebug() << "etFlag county: " << country << " locales: " << locales;
    if (locales.length() > 0) {
        QString short_name = locales.at(0).name().section("_", 1, 1).toLower();
        return QIcon("/usr/share/flags-common/" + short_name + ".png");
    }
    return QIcon();
}

// detect fastest Debian repo
void MainWindow::pushFastestDebian_clicked()
{
    progress->show();
    QTemporaryFile tmpfile;
    if (!tmpfile.open()) {
        qDebug() << "Could not create temp file";
        return;
    }

    QString ver_name {getDebianVerName(getDebianVerNum())};
    if (ver_name == "buster" || ver_name == "bullseye") ver_name = QString(); // netselect-apt doesn't like name buster/bullseye for some reason, maybe it expects "stable"

    QByteArray out;
    bool success = shell->run("netselect-apt " + ver_name + " -o " + tmpfile.fileName(), out, false);
    progress->hide();

    if (!success) {
        QMessageBox::critical(this, tr("Error"), tr("netselect-apt could not detect fastest repo."));
        return;
    }
    QString repo = shell->getCmdOut("set -o pipefail; grep -m1 '^deb ' " + tmpfile.fileName() + "| cut -d' ' -f2");
    this->blockSignals(false);

    if (success && checkRepo(repo)) {
        replaceDebianRepos(repo);
        refresh();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Could not detect fastest repo."));
    }
}

// detect and select the fastest MX repo
void MainWindow::pushFastestMX_clicked()
{
    progress->show();
    QByteArray out;
    bool success = shell->run("set -o pipefail; netselect -D -I " + listMXurls + " |tr -s ' ' |sed 's/^ //' |cut -d' ' -f2", out);
    qDebug() << listMXurls;
    qDebug() << "FASTEST " << success << out;
    progress->hide();
    if (success && !out.isEmpty()) {
        displaySelected(out);
        pushOk_clicked();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Could not detect fastest repo."));
    }
}

//void MainWindow::pushRedirector_clicked()
//{
//    replaceDebianRepos("https://deb.debian.org/debian/");
//    refresh();
//}

void MainWindow::lineSearch_textChanged(const QString &arg1)
{
    displayMXRepos(repos, arg1);
}

void MainWindow::pb_restoreSources_clicked()
{
    // check if running on antiX/MX
    if (!QFileInfo::exists("/etc/antix-version") && !QFileInfo::exists("/etc/mx-version")) {
        QMessageBox::critical(this, tr("Error"), tr("Can't figure out if this app is running on antiX or MX"));
        return;
    }

    bool ok = true;
    int mx_version = shell->getCmdOut("grep -oP '(?<=DISTRIB_RELEASE=).*' /etc/lsb-release").leftRef(2).toInt(&ok);
    if (!ok || mx_version < 15) {
        QMessageBox::critical(this, tr("Error"), tr("MX version not detected or out of range: ") + QString::number(mx_version));
        return;
    }

    QTemporaryDir tmpdir;
    if (!tmpdir.isValid()) {
        qDebug() << "Could not create temp dir";
        return;
    }
    QDir::setCurrent(tmpdir.path());

    // download source files from
    const QString branch = (mx_version > 19) ? "main" : "master";
    const QString url = QString("https://codeload.github.com/MX-Linux/MX-%1_sources/zip/"
                                + branch).arg(QString::number(mx_version));
    QFileInfo fi(url);
    QFile tofile(tmpdir.path() + "/" + fi.fileName() + ".zip");
    if (!downloadFile(url, tofile)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not download original APT files."));
        return;
    }
    // extract master.zip to temp folder
    QString cmd = QString("unzip -q " + tofile.fileName() + " -d %1/").arg(tmpdir.path());
    if (!tofile.exists() || !shell->run(cmd)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not unzip downloaded file."));
        return;
    }
    // move the files from the temporary directory to /etc/apt/sources.list.d/
    cmd = QString("mv -b %1/MX-*_sources-" + branch + "/* /etc/apt/sources.list.d/").arg(tmpdir.path());
    system(cmd.toUtf8());

    // for 64-bit OS check if user wants AHS repo
    if (mx_version >= 19 && shell->getCmdOut("uname -m", true) == "x86_64")
        if (QMessageBox::Yes == QMessageBox::question(this, tr("Enabling AHS"), tr("Do you use AHS (Advanced Hardware Stack) repo?")))
            shell->run("sed -i '/^\\s*#*\\s*deb.*ahs\\s*/s/^#*\\s*//' /etc/apt/sources.list.d/mx.list", true);

    refresh();
    QMessageBox::information(this, tr("Success"),
                             tr("Original APT sources have been restored to the release status. User added source files in /etc/apt/sources.list.d/ have not been touched.") + "\n\n" +
                             tr("Your new selection will take effect the next time sources are updated."));
}

bool MainWindow::checkRepo(const QString &repo)
{
    QNetworkRequest request;
    request.setRawHeader("User-Agent", qApp->applicationName().toUtf8() + "/" + qApp->applicationVersion().toUtf8() + " (linux-gnu)");
    request.setUrl(QUrl(repo));
    reply = manager.head(request);

    auto error {QNetworkReply::NoError};
    QEventLoop loop;
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), [&error](const QNetworkReply::NetworkError &err) {error = err;} ); // errorOccured only in Qt >= 5.15
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, [&loop, &error]() {error = QNetworkReply::TimeoutError; loop.quit();} ); // manager.setTransferTimeout(time) // only in Qt >= 5.15
    loop.exec();
    reply->disconnect();
    if (error == QNetworkReply::NoError)
        return true;
    qDebug() << "No reponse from repo:" << reply->url() << error;
    return false;
}

bool MainWindow::downloadFile(const QString &url, QFile &file)
{
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Could not open file:" << file.fileName();
        return false;
    }

    QNetworkRequest request;
    request.setRawHeader("User-Agent", qApp->applicationName().toUtf8() + "/" + qApp->applicationVersion().toUtf8() + " (linux-gnu)");
    request.setUrl(QUrl(url));

    reply = manager.get(request);
    QEventLoop loop;

    bool success = true;
    connect(reply, &QNetworkReply::readyRead, [this, &file, &success]() { success = file.write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->disconnect();

    if (!success) {
        QMessageBox::warning(this, tr("Error"), tr("There was an error writing file: %1. Please check if you have enough free space on your drive").arg(file.fileName()));
        exit(EXIT_FAILURE);
    }

    file.close();
    return (reply->error() == QNetworkReply::NoError);
}
