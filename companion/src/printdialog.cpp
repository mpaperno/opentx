#include "printdialog.h"
#include "ui_printdialog.h"
#include "helpers.h"
#include <QPrinter>
#include <QPrintDialog>

// #if !defined WIN32 && defined __GNUC__
// #include <unistd.h>
// #endif

PrintDialog::PrintDialog(QWidget *parent, Firmware * firmware, GeneralSettings & generalSettings, ModelData & model, const QString & filename) :
  QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
  firmware(firmware),
  generalSettings(generalSettings),
  model(model),
  printfilename(filename),
  ui(new Ui::PrintDialog),
  multimodelprinter(firmware)
{
  ui->setupUi(this);
  setWindowIcon(CompanionIcon("print.png"));
  setWindowTitle(model.name);
  multimodelprinter.setModel(0, model);
  ui->textEdit->setHtml(multimodelprinter.print(ui->textEdit->document()));
  if (!printfilename.isEmpty()) {
    printToFile();
    QTimer::singleShot(0, this, SLOT(autoClose()));
  }
}

void PrintDialog::closeEvent(QCloseEvent *event) 
{
}

PrintDialog::~PrintDialog()
{
  delete ui;
}

void PrintDialog::on_printButton_clicked()
{
  QPrinter printer;
  printer.setPageMargins(10.0,10.0,10.0,10.0,printer.Millimeter);
  QPrintDialog *dialog = new QPrintDialog(&printer, this);
  dialog->setWindowTitle(tr("Print Document"));
  if (dialog->exec() != QDialog::Accepted)
    return;
  ui->textEdit->print(&printer);
}

void PrintDialog::on_printFileButton_clicked()
{
  QString fn = QFileDialog::getSaveFileName(this,tr("Select PDF output file"),QString(),tr("ODF files (*.odt);;PDF Files(*.pdf);;HTML-Files (*.htm *.html);;All Files (*)"));
  if (fn.isEmpty())
    return;
  if (! (fn.endsWith(".odt", Qt::CaseInsensitive) || fn.endsWith(".pdf", Qt::CaseInsensitive) || fn.endsWith(".htm", Qt::CaseInsensitive) || fn.endsWith(".html", Qt::CaseInsensitive)) )
    fn += ".pdf"; // default
  if (fn.endsWith(".pdf", Qt::CaseInsensitive)) {
    QPrinter printer;
    printer.setPageMargins(10.0,10.0,10.0,10.0,printer.Millimeter);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setColorMode(QPrinter::Color);
    printer.setOutputFileName(fn);
    ui->textEdit->print(&printer);
  }
  else {
    QTextDocumentWriter writer(fn);
     writer.write(ui->textEdit->document());
  }
}

void PrintDialog::printToFile()
{
  if (printfilename.isEmpty())
    return;
  if (! (printfilename.endsWith(".odt", Qt::CaseInsensitive) || printfilename.endsWith(".pdf", Qt::CaseInsensitive) || printfilename.endsWith(".htm", Qt::CaseInsensitive) || printfilename.endsWith(".html", Qt::CaseInsensitive)) )
    printfilename += ".pdf"; // default
  if (printfilename.endsWith(".pdf", Qt::CaseInsensitive)) {
    QPrinter printer;
    printer.setPageMargins(10.0,10.0,10.0,10.0,printer.Millimeter);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setColorMode(QPrinter::Color);
    printer.setOutputFileName(printfilename);
    ui->textEdit->print(&printer);
  }
  else {
    QTextDocumentWriter writer(printfilename);
    writer.write(ui->textEdit->document());
  }
}

void PrintDialog::autoClose()
{
  this->close();
}
