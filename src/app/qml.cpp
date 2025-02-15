// Copyright (c) 2019-2022 The Open-Transactions developers
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "app/imp.hpp"  // IWYU pragma: associated

#include <opentxs/interface/qt/IdentityManager.hpp>
#include <opentxs/interface/qt/SeedValidator.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QStringLiteral>
#include <QUrl>
#include <atomic>
#include <future>

#include "api/api.hpp"
#include "app/size.hpp"
#include "app/startup.hpp"
#include "models/seedlang.hpp"
#include "models/seedsize.hpp"
#include "models/seedtype.hpp"
#include "util/claim.hpp"

namespace metier
{
struct QmlApp final : public App::Imp, public QGuiApplication {
private:
    std::promise<void> model_promise_;

public:
    static std::unique_ptr<App> singleton_;

    App& parent_;
    std::unique_ptr<Api> ot_;
    QQuickView qml_;
    Startup startup_;
    std::atomic<bool> model_init_;
    std::shared_future<void> models_set_;

    auto displayBlockchainChooser() -> void final
    {
        // NOTE when the app.displayBlockchainChooser signal is received the
        // user must enable at least one blockchain.
        //
        // Use otwrap.BlockchainChooserModel to populate a control which allows
        // the user to enable a blockchain.
        //
        // Do not allow the user to proceed until the otwrap.chainsChanged
        // signal has been received with a value greater than zero.
        //
        // Call otwrap.checkStartupConditions() once the user
        // has selected at least one blockchain and is ready to move on.
        models_set_.get();
        startup_.doDisplayBlockchainChooser();
    }

    auto displayFirstRun() -> void final
    {
        // NOTE when the app.displayFirstRun signal is received the user must
        // choose to either create a new seed or recover an existing seed.
        //
        // If the user wants to create a new seed, call otwrap.createNewSeed()
        // then display the words for the user to write down. Once the user is
        // ready to move on, call otwrap.checkStartupConditions()
        //
        // If the user wants to import an existing seed, collect his input then
        // call otwrap.importSeed()
        startup_.doDisplayFirstRun();
    }

    auto displayMainWindow() -> void final
    {
        // NOTE once the app.displayMainWindow signal is received the startup
        // process is complete. Display the main screen.
        models_set_.get();
        startup_.doDisplayMainWindow();
    }

    auto displayNamePrompt() -> void final
    {
        // NOTE when the app.displayNamePrompt signal is received the user must
        // provide a profile name. Call otwrap.createNym() with the name the
        // user provides
        startup_.doDisplayNamePrompt();
    }

    auto displayPasswordPrompt(QString, bool) -> void final
    {
        // TODO
    }

    auto confirmPassword(
        [[maybe_unused]] QString prompt,
        [[maybe_unused]] QString key) -> QString final
    {
        return "opentxs";  // TODO
    }

    auto getPassword(
        [[maybe_unused]] QString prompt,
        [[maybe_unused]] QString key) -> QString final
    {
        return "opentxs";  // TODO
    }

    auto init(int& argc, char** argv) noexcept -> void final
    {
        ot_ = std::make_unique<Api>(*this, parent_, argc, argv);

        {
            auto* ot = ot_.get();
            using IdentityManager = opentxs::ui::IdentityManagerQt;
            auto* identity = ot->identityManager();
            ot->connect(
                identity,
                &IdentityManager::activeNymChanged,
                this,
                &QmlApp::nymReady);
            Ownership::Claim(ot);
            qml_.rootContext()->setContextProperty("api", ot);
        }

        qml_.connect(
            qml_.engine(), &QQmlEngine::quit, this, &QCoreApplication::quit);
        qml_.setSource(QUrl("qrc:/main.qml"));

        if (qml_.status() == QQuickView::Error) { abort(); }

        qml_.setResizeMode(QQuickView::SizeRootObjectToView);
        const auto dSize = qml_default_size();
        const auto mSize = qml_minimum_size();
        const auto xSize = qml_maximum_size();

        if ((0 != dSize.first) && (0 != dSize.second)) {
            qml_.resize(dSize.first, dSize.second);
        }

        if ((0 != mSize.first) && (0 != mSize.second)) {
            qml_.setMinimumSize(QSize{mSize.first, mSize.second});
        }

        if ((0 != xSize.first) && (0 != xSize.second)) {
            qml_.setMaximumSize(QSize{xSize.first, xSize.second});
        }

        qml_.show();
    }

    auto run() -> int final
    {
        setWindowIcon(QIcon(":/qwidget/assets/app_icon.png"));

        return exec();
    }

    auto otwrap() noexcept -> Api* final { return ot_.get(); }

    QmlApp(App& parent, int& argc, char** argv) noexcept
        : QGuiApplication(argc, argv)
        , model_promise_()
        , parent_(parent)
        , ot_()
        , qml_()
        , startup_()
        , model_init_(false)
        , models_set_(model_promise_.get_future())
    {
        {
            auto* app = &startup_;
            Ownership::Claim(app);
            qml_.rootContext()->setContextProperty("startup", app);
        }

        Ownership::Claim(this);
        qml_.rootContext()->setContextProperty("metier", this);
    }

    ~QmlApp() final = default;

private:
    auto nymReady() noexcept -> void
    {
        const auto init = model_init_.exchange(true);

        if (init) { return; }

        model_promise_.set_value();
    }
};

auto App::Imp::factory_qml(App& parent, int& argc, char** argv) noexcept
    -> std::unique_ptr<Imp>
{
    return std::make_unique<QmlApp>(parent, argc, argv);
}
}  // namespace metier

namespace metier
{
auto App::Imp::choose_interface(
    App& parent,
    int& argc,
    char** argv,
    bool advanced) noexcept -> std::unique_ptr<Imp>
{
    if (advanced) {

        return factory_widgets(parent, argc, argv);
    } else {

        return factory_qml(parent, argc, argv);
    }
}
}  // namespace metier
