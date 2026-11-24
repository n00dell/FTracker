// FTracker/gui/src/main.cpp
#include "app-window.h" // generated header from ui/app-window.slint
#include "backend/Controllers/DashController.h"
int main(int argc, char **argv)
{
    auto main_window = MainWindow::create();
    main_window->run();

    // pesa::DashController dash(ui);
    // dash.init();
    //
    // ui->run();
    return 0;

}