/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mdichild.h"
#include "ui_mdichild.h"
#include "mainwindow.h"
#include "modeledit/modeledit.h"
#include "generaledit/generaledit.h"
#include "burnconfigdialog.h"
#include "printdialog.h"
#include "flasheepromdialog.h"
#include "helpers.h"
#include "appdata.h"
#include "wizarddialog.h"
#include "flashfirmwaredialog.h"
#include "storage.h"
#include "radiointerface.h"

#if defined _MSC_VER || !defined __GNUC__
#include <windows.h>
#define sleep(x) Sleep(x*1000)
#else
#include <unistd.h>
#endif

MdiChild::MdiChild(MainWindow * parent):
  QWidget(),
  parent(parent),
  ui(new Ui::MdiChild),
  modelsListModel(NULL),
  firmware(getCurrentFirmware()),
  isUntitled(true),
  fileChanged(false)
{
  ui->setupUi(this);
  setWindowIcon(CompanionIcon("open.png"));
  ui->simulateButton->setIcon(CompanionIcon("simulate.png"));
  setAttribute(Qt::WA_DeleteOnClose);
  initModelsList();
  connect(parent, SIGNAL(FirmwareChanged()), this, SLOT(onFirmwareChanged()));
  connect(ui->modelsList, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(openModelEditWindow()));
  connect(ui->modelsList, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(showModelsListContextMenu(const QPoint &)));
  // connect(ui->modelsList, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this, SLOT(onCurrentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)));

  ui->modelsList->setContextMenuPolicy(Qt::CustomContextMenu);
  ui->modelsList->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui->modelsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  // ui->modelsList->setDragEnabled(true);
  // ui->modelsList->setAcceptDrops(true);
  // ui->modelsList->setDragDropOverwriteMode(true);
  // ui->modelsList->setDropIndicatorShown(true);

  if (!(isMaximized() || isMinimized())) {
    adjustSize();
  }
}

MdiChild::~MdiChild()
{
  delete ui;
}

void MdiChild::refresh(bool expand)
{
  modelsListModel->refresh();
  if (1 || expand) {
    ui->modelsList->expandAll();
  }
  ui->simulateButton->setEnabled(firmware->getCapability(Simulation));
  updateTitle();
}

void MdiChild::confirmDelete()
{
  if (QMessageBox::warning(this, "Companion", tr("Delete selected models?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    deleteSelectedModels();
  }
}

void MdiChild::deleteSelectedModels()
{
  foreach (QModelIndex index, ui->modelsList->selectionModel()->selectedIndexes()) {
    if (index.column() == 0) {
      unsigned int modelIndex = modelsListModel->getModelIndex(index);
      if (radioData.generalSettings.currModelIndex != modelIndex) {
        qDebug() << "delete" << modelIndex;
        radioData.models[modelIndex].clear();
        setModified();
      }
      else {
        QMessageBox::warning(this, "Companion", tr("Cannot delete default model."), QMessageBox::Ok);
      }
    }
  }
}

void MdiChild::showModelsListContextMenu(const QPoint & pos)
{
  QModelIndex modelIndex = ui->modelsList->indexAt(pos);
  QPoint globalPos = ui->modelsList->mapToGlobal(pos);
  QMenu contextMenu;
  if (modelsListModel->getModelIndex(modelIndex) >= 0) {
    const QClipboard * clipboard = QApplication::clipboard();
    const QMimeData * mimeData = clipboard->mimeData();
    bool hasData = mimeData->hasFormat("application/x-companion");
    contextMenu.addAction(CompanionIcon("edit.png"), tr("&Edit"), this, SLOT(modelEdit()));
    contextMenu.addAction(CompanionIcon("open.png"), tr("&Restore from backup"), this, SLOT(loadBackup()));
    contextMenu.addAction(CompanionIcon("wizard.png"), tr("&Model Wizard"), this, SLOT(wizardEdit()));
    contextMenu.addSeparator();
    contextMenu.addAction(CompanionIcon("clear.png"), tr("&Delete"), this, SLOT(confirmDelete()), tr("Delete"));
    contextMenu.addAction(CompanionIcon("copy.png"), tr("&Copy"), this, SLOT(copy()), tr("Ctrl+C"));
    contextMenu.addAction(CompanionIcon("cut.png"), tr("&Cut"), this, SLOT(cut()), tr("Ctrl+X"));
    contextMenu.addAction(CompanionIcon("paste.png"), tr("&Paste"), this, SLOT(paste()), tr("Ctrl+V"))->setEnabled(hasData);
    contextMenu.addAction(CompanionIcon("duplicate.png"), tr("D&uplicate"), this, SLOT(duplicate()), tr("Ctrl+U"));
    contextMenu.addSeparator();
    contextMenu.addAction(CompanionIcon("currentmodel.png"), tr("&Use as default"), this, SLOT(setDefault()));
    contextMenu.addSeparator();
    contextMenu.addAction(CompanionIcon("print.png"), tr("P&rint model"), this, SLOT(print()), QKeySequence(tr("Ctrl+P")));
    contextMenu.addSeparator();
    contextMenu.addAction(CompanionIcon("simulate.png"), tr("&Simulate model"), this, SLOT(modelSimulate()), tr("Alt+S"));
  }
  else if (IS_HORUS(firmware->getBoard())) {
    if (modelsListModel->getCategoryIndex(modelIndex) >= 0) {
      contextMenu.addAction(CompanionIcon("add.png"), tr("&Add model"), this, SLOT(modelAdd()));
      contextMenu.addAction(CompanionIcon("delete.png"), tr("&Delete category"), this, SLOT(categoryDelete()));
    }
    else {
      contextMenu.addAction(CompanionIcon("add.png"), tr("&Add category"), this, SLOT(categoryAdd()));
    }
  }
  else {
    return;
  }

  contextMenu.exec(globalPos);
}

void MdiChild::onFirmwareChanged()
{
  Firmware * previous = firmware;
  firmware = getCurrentFirmware();
  qDebug() << "onFirmwareChanged" << previous->getName() << "=>" << firmware->getName();
  radioData.convert(previous, firmware);
  initModelsList();
}

void MdiChild::initModelsList()
{
  BoardEnum board = firmware->getBoard();

  delete modelsListModel;
  modelsListModel = new TreeModel(&radioData, this);
  connect(modelsListModel, SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(onDataChanged(const QModelIndex &)));
  ui->modelsList->setModel(modelsListModel);
  ui->modelsList->header()->setVisible(!IS_HORUS(board));
  if (IS_HORUS(board)) {
    ui->modelsList->setIndentation(20);
    // ui->modelsList->resetIndentation(); // introduced in next Qt versions ...
  }
  else {
    ui->modelsList->setIndentation(0);
  }
  refresh();
}

void MdiChild::cut()
{
  copy();
  deleteSelectedModels();
}

void MdiChild::copy()
{
  QByteArray gmData;
  doCopy(&gmData);

  QMimeData * mimeData = new QMimeData;
  mimeData->setData("application/x-companion", gmData);

  QClipboard * clipboard = QApplication::clipboard();
  clipboard->setMimeData(mimeData, QClipboard::Clipboard);
}

void MdiChild::doCopy(QByteArray * gmData)
{
  foreach(QModelIndex index, ui->modelsList->selectionModel()->selectedIndexes()) {
    if (index.column() == 0) {
      unsigned int modelIndex = modelsListModel->getModelIndex(index);
      if (modelIndex >= 0) {
        gmData->append('M');
        gmData->append((char *) &radioData.models[modelIndex], sizeof(ModelData));
      }
    }
  }

// TODO to copy radio settings
// gmData->append('G');
// gmData->append((char *) &radioData.generalSettings, sizeof(GeneralSettings));
}

void MdiChild::doPaste(QByteArray * gmData, int index)
{
  char * gData = gmData->data();
  bool modified = false;
  int size = 0;

  while (size < gmData->size()) {
    char c = *gData++;
    size++;
    if (c == 'G') {
      // General settings
      int ret = QMessageBox::question(this, "Companion", tr("Do you want to overwrite radio general settings?"),
                                  QMessageBox::Yes | QMessageBox::No);
      if (ret == QMessageBox::Yes) {
        radioData.generalSettings = *((GeneralSettings *)gData);
        modified = 1;
      }
      gData += sizeof(GeneralSettings);
      size += sizeof(GeneralSettings);
    }
    else if (c == 'M') {
      if (firmware->getCapability(Models) == 0 || index < firmware->getCapability(Models)) {
        // Model data
        int ret = QMessageBox::Yes;
        if (!radioData.models[index].isEmpty()) {
          ret = QMessageBox::question(this, "Companion", tr("You are pasting on an not empty model, are you sure?"),
                                      QMessageBox::Yes | QMessageBox::No);
        }
        if (ret == QMessageBox::Yes) {
          radioData.models[index] = *((ModelData *)gData);
          strcpy(radioData.models[index].filename, radioData.getNextModelFilename().toStdString().c_str());
          modified = 1;
        }
        gData += sizeof(ModelData);
        size += sizeof(ModelData);
        index++;
      }
    }
    else {
      qWarning() << "paste error";
      break;
    }
  }
  if (modified) {
    setModified();
  }
}

void MdiChild::paste()
{
  if (hasPasteData()) {
    const QClipboard * clipboard = QApplication::clipboard();
    const QMimeData * mimeData = clipboard->mimeData();
    QByteArray gmData = mimeData->data("application/x-companion");
    doPaste(&gmData, getCurrentRow());
  }
}

bool MdiChild::hasPasteData() const
{
  const QClipboard * clipboard = QApplication::clipboard();
  const QMimeData * mimeData = clipboard->mimeData();
  return mimeData->hasFormat("application/x-companion");
}

bool MdiChild::hasSelection() const
{
  return ui->modelsList->selectionModel()->hasSelection();
}

void MdiChild::updateTitle()
{
  QString title = userFriendlyCurrentFile() + "[*]" + " (" + firmware->getName() + QString(")");
  int availableEEpromSize = modelsListModel->getAvailableEEpromSize();
  if (availableEEpromSize >= 0) {
    title += QString(" - %1 ").arg(availableEEpromSize) + tr("free bytes");
  }
  setWindowTitle(title);
}

void MdiChild::setModified()
{
  refresh();
  fileChanged = true;
  documentWasModified();
}

void MdiChild::keyPressEvent(QKeyEvent * event)
{
  if (event->matches(QKeySequence::Delete)) {
    deleteSelectedModels();
  }
  else if (event->matches(QKeySequence::Cut)) {
    cut();
  }
  else if (event->matches(QKeySequence::Copy)) {
    copy();
  }
  else if (event->matches(QKeySequence::Paste)) {
    paste();
  }
  else if (event->matches(QKeySequence::Underline)) {
    // TODO duplicate();
  }
  else {
    QWidget::keyPressEvent(event); // run the standard event in case we didn't catch an action
  }
}

void MdiChild::on_simulateButton_clicked()
{
  radioSimulate();
}

void MdiChild::checkAndInitModel(int row)
{
  ModelData & model = radioData.models[row];
  if (model.isEmpty()) {
    model.setDefaultValues(row, radioData.generalSettings);
    setModified();
  }
}

void MdiChild::generalEdit()
{
  GeneralEdit * t = new GeneralEdit(this, radioData, firmware);
  connect(t, SIGNAL(modified()), this, SLOT(setModified()));
  t->show();
}

void MdiChild::categoryAdd()
{
  CategoryData category("New category");
  radioData.categories.push_back(category);
  setModified();
}

void MdiChild::categoryDelete()
{
  int categoryIndex = modelsListModel->getCategoryIndex(ui->modelsList->currentIndex());
  if (categoryIndex < 0 || categoryIndex >= (int)radioData.categories.size()) {
    return;
  }

  for (unsigned i=0; i<radioData.models.size(); i++) {
    ModelData & model = radioData.models[i];
    if (model.used && model.category == categoryIndex) {
      QMessageBox::warning(this, tr("Companion"),
                                 tr("This category is not empty!"),
                                 QMessageBox::Cancel);
      return;
    }
  }

  radioData.categories.erase(radioData.categories.begin() + categoryIndex);
  for (unsigned i=0; i<radioData.models.size(); i++) {
    ModelData & model = radioData.models[i];
    if (model.used && model.category > categoryIndex) {
      model.category--;
    }
  }

  setModified();
}

void MdiChild::onDataChanged(const QModelIndex & index)
{
  int modelIndex = modelsListModel->getModelIndex(index);
  if (modelIndex >= 0) {
    return;
  }
  int categoryIndex = modelsListModel->getCategoryIndex(index);
  if (categoryIndex < 0 || categoryIndex >= (int)radioData.categories.size()) {
    return;
  }
  strcpy(radioData.categories[categoryIndex].name, modelsListModel->data(index, 0).toString().left(sizeof(CategoryData::name)-1).toStdString().c_str());
  fileChanged = true;
  documentWasModified();
}

void MdiChild::modelAdd()
{
  int categoryIndex = modelsListModel->getCategoryIndex(ui->modelsList->currentIndex());
  if (categoryIndex < 0 || categoryIndex >= (int)radioData.categories.size()) {
    return;
  }

  ModelData model;
  model.category = categoryIndex;
  model.used = true;
  sprintf(model.filename, "model%lu.bin", (long unsigned)radioData.models.size()+1);
  strcpy(model.name, tr("New model").toStdString().c_str());
  radioData.models.push_back(model);
  radioData.setCurrentModel(radioData.models.size() - 1);
  setModified();
}

void MdiChild::modelEdit()
{
  int row = getCurrentRow();
  QApplication::setOverrideCursor(Qt::WaitCursor);
  checkAndInitModel(row);
  ModelData & model = radioData.models[row];
  gStopwatch.restart();
  gStopwatch.report("ModelEdit creation");
  ModelEdit * t = new ModelEdit(this, radioData, (row), firmware);
  gStopwatch.report("ModelEdit created");
  t->setWindowTitle(tr("Editing model %1: ").arg(row+1) + model.name);
  connect(t, SIGNAL(modified()), this, SLOT(setModified()));
  gStopwatch.report("STARTING MODEL EDIT");
  t->show();
  QApplication::restoreOverrideCursor();
  gStopwatch.report("ModelEdit shown");
}

void MdiChild::setDefault()
{
  int row = getCurrentRow();
  if (!radioData.models[row].isEmpty() && radioData.generalSettings.currModelIndex != (unsigned)row) {
    radioData.setCurrentModel(row);
    setModified();
  }
}

void MdiChild::wizardEdit()
{
  int row = getCurrentRow();
  checkAndInitModel(row);
  WizardDialog * wizard = new WizardDialog(radioData.generalSettings, row+1, this);
  wizard->exec();
  if (wizard->mix.complete /*TODO rather test the exec() result?*/) {
    radioData.models[row] = wizard->mix;
    setModified();
  }
}

void MdiChild::openModelEditWindow()
{
  int row = getCurrentRow();
  if (row >= 0) {
    ModelData & model = radioData.models[row];
    if (model.isEmpty() && g.useWizard()) {
      wizardEdit();
    }
    else {
      modelEdit();
    }
  }
}

void MdiChild::newFile()
{
  static int sequenceNumber = 1;
  isUntitled = true;
  curFile = QString("document%1.otx").arg(sequenceNumber++);
  updateTitle();
}

bool MdiChild::loadFile(const QString & filename, bool resetCurrentFile)
{
  Storage storage(filename);
  if (!storage.load(radioData)) {
    QMessageBox::critical(this, tr("Error"), storage.error());
    return false;
  }

  QString warning = storage.warning();
  if (!warning.isEmpty()) {
    // TODO ShowEepromWarnings(this, tr("Warning"), warning);
  }

  refresh(true);
  if (resetCurrentFile)
    setCurrentFile(filename);

  return true;
}

bool MdiChild::save()
{
  if (isUntitled) {
    return saveAs(true);
  }
  else {
    return saveFile(curFile);
  }
}

bool MdiChild::saveAs(bool isNew)
{
  QString fileName;
  if (IS_SKY9X(getCurrentBoard())) {
    curFile.replace(".eepe", ".bin");
    QFileInfo fi(curFile);
#ifdef __APPLE__
    fileName = QFileDialog::getSaveFileName(this, tr("Save As"), g.eepromDir() + "/" +fi.fileName());
#else
    fileName = QFileDialog::getSaveFileName(this, tr("Save As"), g.eepromDir() + "/" +fi.fileName(), tr(BIN_FILES_FILTER));
#endif
  }
  else {
    QFileInfo fi(curFile);
#ifdef __APPLE__
    fileName = QFileDialog::getSaveFileName(this, tr("Save As"), g.eepromDir() + "/" +fi.fileName());
#else
    fileName = QFileDialog::getSaveFileName(this, tr("Save As"), g.eepromDir() + "/" +fi.fileName(), tr(EEPROM_FILES_FILTER));
#endif
  }
  if (fileName.isEmpty())
    return false;
  g.eepromDir( QFileInfo(fileName).dir().absolutePath() );
  if (isNew)
    return saveFile(fileName);
  else
    return saveFile(fileName,true);
}

bool MdiChild::saveFile(const QString & filename, bool setCurrent)
{
  BoardEnum board = getCurrentBoard();
  QString path = filename;
  if (IS_SKY9X(board)) {
    path.replace(".eepe", ".bin");
  }

  radioData.fixModelFilenames();
  Storage storage(path);
  bool result = storage.write(radioData);

  if (result && setCurrent) {
    setCurrentFile(path);
  }

  return result;
}

QString MdiChild::userFriendlyCurrentFile() const
{
  return QFileInfo(curFile).fileName();
}

void MdiChild::closeEvent(QCloseEvent *event)
{
  if (maybeSave()) {
    event->accept();
  }
  else {
    event->ignore();
  }
}

void MdiChild::documentWasModified()
{
  setWindowModified(fileChanged);
}

bool MdiChild::maybeSave()
{
  if (fileChanged) {
    QMessageBox::StandardButton ret;
    ret = QMessageBox::warning(this, tr("Companion"),
        tr("%1 has been modified.\n"
           "Do you want to save your changes?").arg(userFriendlyCurrentFile()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save)
      return save();
    else if (ret == QMessageBox::Cancel)
      return false;
  }
  return true;
}

void MdiChild::setCurrentFile(const QString &fileName)
{
  curFile = QFileInfo(fileName).canonicalFilePath();
  isUntitled = false;
  fileChanged = false;
  setWindowModified(false);
  updateTitle();
  int MaxRecentFiles = g.historySize();
  QStringList files = g.recentFiles();
  files.removeAll(fileName);
  files.prepend(fileName);
  while (files.size() > MaxRecentFiles)
    files.removeLast();
  g.recentFiles(files);
}

void MdiChild::writeEeprom()  // write to Tx
{
  BoardEnum board = getCurrentBoard();
  if (board == BOARD_HORUS) {
    QString radioPath = findMassstoragePath("RADIO", true);
    qDebug() << "Searching for SD card, found" << radioPath;
    if (radioPath.isEmpty()) {
      qDebug() << "MdiChild::writeEeprom(): Horus radio not found";
      QMessageBox::critical(this, tr("Error"), tr("Unable to find Horus radio SD card!"));
      return;
    }
    if (saveFile(radioPath, false)) {
      parent->statusBar()->showMessage(tr("Models and Settings written"), 2000);
    }
    else {
      qDebug() << "MdiChild::writeEeprom(): saveFile error";
    }
  }
  else {
    QString tempFile = generateProcessUniqueTempFileName("temp.bin");
    saveFile(tempFile, false);
    if (!QFileInfo(tempFile).exists()) {
      QMessageBox::critical(this, tr("Error"), tr("Cannot write temporary file!"));
      return;
    }
    FlashEEpromDialog * cd = new FlashEEpromDialog(this, tempFile);
    cd->exec();
  }
}

void MdiChild::on_radioSettings_clicked()
{
  generalEdit();
}

void MdiChild::radioSimulate()
{
  startSimulation(this, radioData, -1);
}

void MdiChild::modelSimulate()
{
  startSimulation(this, radioData, getCurrentRow());
}

void MdiChild::print(int model, const QString & filename)
{
  // TODO
  PrintDialog * pd = NULL;

  if (model>=0 && !filename.isEmpty()) {
    pd = new PrintDialog(this, firmware, radioData.generalSettings, radioData.models[model], filename);
  }
  else if (getCurrentRow()) {
    pd = new PrintDialog(this, firmware, radioData.generalSettings, radioData.models[getCurrentRow()]);
  }

  if (pd) {
    pd->setAttribute(Qt::WA_DeleteOnClose, true);
    pd->show();
  }
}

void MdiChild::viableModelSelected(bool viable)
{
  emit copyAvailable(viable);
}

int MdiChild::getCurrentRow() const
{
  return modelsListModel->getModelIndex(ui->modelsList->currentIndex());
}

bool MdiChild::loadBackup()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Open backup Models and Settings file"), g.eepromDir(), tr(EEPROM_FILES_FILTER));
  if (fileName.isEmpty())
    return false;
  QFile file(fileName);

  if (!file.exists()) {
    QMessageBox::critical(this, tr("Error"), tr("Unable to find file %1!").arg(fileName));
    return false;
  }

  // TODO int index = getCurrentRow();

  int eeprom_size = file.size();
  if (!file.open(QFile::ReadOnly)) {  //reading binary file   - TODO HEX support
    QMessageBox::critical(this, tr("Error"),
                          tr("Error opening file %1:\n%2.")
                          .arg(fileName)
                          .arg(file.errorString()));
    return false;
  }
  QByteArray eeprom(eeprom_size, 0);
  long result = file.read((char*)eeprom.data(), eeprom_size);
  file.close();

  if (result != eeprom_size) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Error reading file %1:\n%2.")
                          .arg(fileName)
                          .arg(file.errorString()));

    return false;
  }

#if 0
  std::bitset<NUM_ERRORS> errorsEeprom((unsigned long long)LoadBackup(radioData, (uint8_t *)eeprom.data(), eeprom_size, index));
  if (!errorsEeprom.test(ALL_OK)) {
    ShowEepromErrors(this, tr("Error"), tr("Invalid binary backup File %1").arg(fileName), (errorsEeprom).to_ulong());
    return false;
  }
  if (errorsEeprom.test(HAS_WARNINGS)) {
    ShowEepromWarnings(this, tr("Warning"), errorsEeprom.to_ulong());
  }

  refresh(true);
  return true;
#else
  return false;
#endif
}
