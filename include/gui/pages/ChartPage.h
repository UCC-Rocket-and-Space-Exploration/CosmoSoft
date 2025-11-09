#ifndef COSMO_SOFT_CHARTPAGE_H
#define COSMO_SOFT_CHARTPAGE_H

#include <QWidget>

// ChartPage encapsulates the QtCharts visualization so MainWindow stays lean.
class ChartPage : public QWidget {
    Q_OBJECT

public:
    explicit ChartPage(QWidget *parent = nullptr);
    ~ChartPage() override = default;
};

#endif // COSMO_SOFT_CHARTPAGE_H
