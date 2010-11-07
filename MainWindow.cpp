#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "PathPaintPage.h"
#include "Plotter.h"

#include "CuttingDialog.h"
#include <QPainter>
#include <QDebug>

#include <QSvgRenderer>

#include <QMessageBox>
#include <QFileDialog>
#include <QDir>

#include <QShortcut>
#include <cmath>
#include <QFile>

#include <QEvent>
#include <iostream>
#include <QWheelEvent>

using namespace std;

const double InitialZoom = 2.0;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	scene = new QGraphicsScene(this);
	ui->graphicsView->setScene(scene);

	ui->graphicsView->scale(InitialZoom, InitialZoom);

	ui->graphicsView->viewport()->installEventFilter(this);

	cutDialog = NULL;

	animationTimer = new QTimer(this);
	connect(animationTimer, SIGNAL(timeout()), SLOT(animate()));
	cutMarker = NULL;

	// Alternative zoom shortcuts
	QShortcut* zoom_in = new QShortcut(QKeySequence("X"), this);
	connect(zoom_in, SIGNAL(activated()), SLOT(on_actionZoom_In_triggered()));
	QShortcut* zoom_out = new QShortcut(QKeySequence("Z"), this);
	connect(zoom_out, SIGNAL(activated()), SLOT(on_actionZoom_Out_triggered()));
	//default options if not specified on command line
	sortFlag=0;
	cutFlag=0;
	fileValue=NULL;
}

MainWindow::~MainWindow()
{
	delete ui;
}

static bool MyLessThan(const QPolygonF &p1, const QPolygonF &p2)
{
	qreal testx1 = p1[0].x();
	qreal testx2 = p2[0].x();
	qreal testy1 = p1[0].y();
	qreal testy2 = p2[0].y();
	if(testy1 == testy2) return testx1 < testx2;
	else return testy1 < testy2;
}

void MainWindow::on_actionOpen_triggered()
{
	if (lastOpenDir.isEmpty())
		lastOpenDir = QDir::homePath();

	filename = QFileDialog::getOpenFileName(this,
			tr("Open File"), lastOpenDir, tr("SVG Files (*.svg)"));
	if (filename.isEmpty())
		return;

	lastOpenDir = QFileInfo(filename).absoluteDir().path();
	loadFile();
}

void MainWindow::loadFile()
{
	if (filename.isEmpty())
		return;
		
	qDebug() << "Reading file: " << filename;

	QSvgRenderer rend;
	if (!rend.load(filename))
	{
		QMessageBox::critical(this, "Error loading file.", "Couldn't open the file for reading.");
		qDebug() << "Error loading svg.";
		return;
	}

	qDebug() << "SVG default size: " << rend.defaultSize() << endl;
	qDebug() << "SVG view box: " << rend.viewBoxF() << endl;

	// Get size from SVG. TODO: Sanity check.
	// Also TODO: This won't handle offset viewboxes... need to get the offset and subtract it from
	// all the objects.
	mediaSize = rend.viewBoxF().size() * 25.4 / 90.0;

	double ppm = 90.0/25.4; // Pixels per mm.

	qDebug() << "Page size (mm): " << mediaSize << endl;

	PathPaintDevice pg(mediaSize.width(), mediaSize.height(), ppm);
	QPainter p(&pg);

	rend.render(&p);

	paths = pg.paths();
	if(sortFlag == true)qSort(paths.begin(), paths.end(), MyLessThan);
	scene->clear();
	scene->setBackgroundBrush(QBrush(Qt::lightGray));

	// The page.
	scene->addRect(0.0, 0.0, mediaSize.width(), mediaSize.height(), QPen(), QBrush(Qt::white));

	QPen pen;
	pen.setWidthF(0.5);
	for (int i = 0, ii = 50, iii=0; i < paths.size(); ++i,ii+=10)
	{
		switch (iii)
		{
		case 0:
			pen.setColor(QColor(ii, ii, ii));
				break;
		case 1:
			pen.setColor(QColor(ii, 0, 0));
				break;
		case 2:
			pen.setColor(QColor(0, ii, 0));
				break;
		case 3:
			pen.setColor(QColor(0, 0, ii));
				break;
		case 4:
			pen.setColor(QColor(ii, ii, 0));
				break;
		case 5:
			pen.setColor(QColor(ii, 0, ii));
				break;
		case 6:
			pen.setColor(QColor(0, ii, ii));
				break;
		}

		scene->addPolygon(paths[i], pen);
		if(ii >= 200)
		{
			ii = 50;
			iii++;
			if(iii >=7)iii=0;
		}
	}

	// Handle the animation. I.e. stop it.
	// The old one was deleted when we cleared the scene.
	cutMarker = scene->addEllipse(-1.0, -1.0, 2.0, 2.0, QPen(Qt::black), QBrush(Qt::red));
	cutMarker->hide();
	ui->actionAnimate->setChecked(false);

	// Reset the viewport.
	on_actionReset_triggered();
	// Redraw. Probably not necessary.
	update();

	if (pg.clipped())
		QMessageBox::warning(this, "Paths clipped", "<b>BIG FAT WARNING!</b><br><br>Some paths lay outside the 210&times;297&thinsp;mm A4 area. These have been squeezed back onto the page in a most ugly fashion, so cutting will almost certainly not do what you want.");

	// Change window title and enable menu items.
	setFileLoaded(filename);
}

void MainWindow::on_actionAbout_triggered()
{
	QMessageBox::information(this, "About", "<b>Robocut 0.2</b><br><br>By Tim Hutt, &copy; 2010<br/><br/>This software allows you to read a vector image in <a href=\"http://en.wikipedia.org/wiki/Scalable_Vector_Graphics\">SVG format</a>, and send it to a <a href=\"http://www.graphteccorp.com/craftrobo/\">Graphtec Craft Robo 2</a> (or possibly 3) for cutting. It is designed to work with SVGs produced by the excellent free vector graphics editor <a href=\"http://www.inkscape.org/\">Inkscape</a>. It may work with other software but this has not been tested.<br/><br/>See <a href=\"http://concentriclivers.com/\">the online manual for instructions</a>.");
}

void MainWindow::on_actionExit_triggered()
{
	close();
}

void MainWindow::on_actionCut_triggered()
{
	if (!cutDialog)
		cutDialog = new CutDialog(this);

	if (cutDialog->exec() != QDialog::Accepted)
		return;

	// Create a new dialog and run the actual cutting in a different thread.

	CuttingDialog* cuttingDlg = new CuttingDialog(this);
	cuttingDlg->startCut(paths, mediaSize.width(), mediaSize.height(), cutDialog->media(), cutDialog->speed(),
	                     cutDialog->pressure(), cutDialog->trackEnhancing(),
	                     cutDialog->regMark(), cutDialog->regSearch(),
	                     cutDialog->regWidth(), cutDialog->regHeight());
	cuttingDlg->show();
}

void MainWindow::on_actionManual_triggered()
{
	QMessageBox::information(this, "Manual", "An online manual is available at <br><br><a href=\"http://concentriclivers.com/\">http://concentriclivers.com/</a>");
}

void MainWindow::on_actionAnimate_toggled(bool animate)
{
	if (animate)
	{
		animationTimer->start(50);
		if (cutMarker)
		{
			cutMarker->setPos(0, 0);
			cutMarker->show();
		}

		cutMarkerPath = 0;
		cutMarkerLine = 0;
		cutMarkerDistance = 0.0;
	}
	else
	{
		animationTimer->stop();
		if (cutMarker)
			cutMarker->hide();
	}
}

void MainWindow::on_actionReset_triggered()
{
	ui->graphicsView->resetTransform();
	ui->graphicsView->scale(InitialZoom, InitialZoom);
}

void MainWindow::on_actionZoom_In_triggered()
{
	ui->graphicsView->scale(1.2, 1.2);
}

void MainWindow::on_actionZoom_Out_triggered()
{
	ui->graphicsView->scale(1.0/1.2, 1.0/1.2);
}

void MainWindow::animate()
{
	if (!cutMarker)
		return;

	// Make sure the current position is sane.
	if (cutMarkerPath >= paths.size() || cutMarkerLine >= paths[cutMarkerPath].size())
	{
		// If not, reset it.
		cutMarkerPath = 0;
		cutMarkerLine = 0;
		cutMarkerDistance = 0.0;
		return;
	}
	// Get the ends of the segment/edge we are currently on.

	QPointF a = paths[cutMarkerPath][cutMarkerLine];
	QPointF b;
	if (cutMarkerLine == paths[cutMarkerPath].size()-1)
	{
		// We are moving between paths, and not cutting.
		b = paths[(cutMarkerPath+1) % paths.size()][0];
		cutMarker->setOpacity(0.3);
	}
	else
	{
		// We are cutting on a path.
		b = paths[cutMarkerPath][cutMarkerLine+1];
		cutMarker->setOpacity(1.0);
	}

	QPointF r = b-a;
	double h = hypot(r.x(), r.y());
	if (h > 0.0)
		r /= h;

	// The current position of the marker.
	QPointF p = a + r * cutMarkerDistance;

	cutMarker->setPos(p);

	// Advance the position of the marker for the next time this function is called.
	cutMarkerDistance += 2.0;
	if (cutMarkerDistance > h)
	{
		cutMarkerDistance = 0.0;
		if (++cutMarkerLine >= paths[cutMarkerPath].size())
		{
			cutMarkerLine = 0;
			if (++cutMarkerPath >= paths.size())
			{
				cutMarkerPath = 0;

				// Also stop the animation...
				ui->actionAnimate->setChecked(false);

			}
		}
	}
}
void MainWindow::setFileLoaded(QString filename)
{
	bool e = !filename.isEmpty();
	if (e)
		setWindowTitle("Robocut - " + filename);
	else
		setWindowTitle("Robocut");

	ui->actionAnimate->setEnabled(e);
	ui->actionReset->setEnabled(e);
	ui->actionZoom_In->setEnabled(e);
	ui->actionZoom_Out->setEnabled(e);
	ui->actionCut->setEnabled(e);

}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
	if (o == ui->graphicsView->viewport())
	{
		if (e->type() == QEvent::Wheel)
		{
			QWheelEvent *w = dynamic_cast<QWheelEvent*>(e);
			if(w->delta() <= 0) on_actionZoom_In_triggered();
			else on_actionZoom_Out_triggered();
			return true;
		}
	}
	return false;
}

void MainWindow::optDone()
{
	filename = QString(fileValue);
	if(QFile::exists(filename)) loadFile();
	if(cutFlag == true) on_actionCut_triggered();
}

