#include <gtest/gtest.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#include <grp.h>
#endif

#include "power-controller.hpp"

/* ─── WFPowerController::create() ────────────────────────────────────────── */

TEST(PowerControllerCreate, ReturnsNonNull)
{
    auto ctrl = WFPowerController::create();
    EXPECT_NE(ctrl, nullptr) << "create() must return a non-null pointer on this platform";
}

/* ─── WFPowerController::is_root() ───────────────────────────────────────── */

TEST(PowerControllerIsRoot, MatchesGeteuid)
{
    EXPECT_EQ(WFPowerController::is_root(), (geteuid() == 0));
}

/* ─── WFPowerController::check_permission() ──────────────────────────────── */

TEST(PowerControllerCheckPermission, NullptrReturnsFalse)
{
    /* A nullptr command should return false */
    bool result = WFPowerController::check_permission(nullptr);
    EXPECT_FALSE(result);
}

TEST(PowerControllerCheckPermission, EmptyCommandReturnsFalse)
{
    bool result = WFPowerController::check_permission("");
    EXPECT_FALSE(result);
}

TEST(PowerControllerCheckPermission, NonExistentCommandReturnsFalse)
{
    EXPECT_FALSE(WFPowerController::check_permission("non_existent_command_xyz_123"));
}

TEST(PowerControllerCheckPermission, ValidCommandReturnsTrue)
{
    // sh should always exist and be executable
    EXPECT_TRUE(WFPowerController::check_permission("sh"));
}

/* ─── WFPowerController::query() ─────────────────────────────────────────── */

class PowerControllerQuery : public ::testing::Test
{
  protected:
    std::unique_ptr<WFPowerController> ctrl;

    void SetUp() override
    {
        ctrl = WFPowerController::create();
        ASSERT_NE(ctrl, nullptr);
    }
};

TEST_F(PowerControllerQuery, ShutdownCapabilityIsValid)
{
    auto cap = ctrl->query(WFPowerController::Action::Shutdown);
    EXPECT_TRUE(cap.available) << "Shutdown should be available on all supported platforms";
}

TEST_F(PowerControllerQuery, RebootCapabilityIsValid)
{
    auto cap = ctrl->query(WFPowerController::Action::Reboot);
    EXPECT_TRUE(cap.available) << "Reboot should be available on all supported platforms";
}

TEST_F(PowerControllerQuery, SuspendCapabilityIsValid)
{
    auto cap = ctrl->query(WFPowerController::Action::Suspend);
    /* Suspend capability is checked on the system, verify consistent status */
    if (cap.available) {
        EXPECT_EQ(cap.permitted, !cap.command.empty())
            << "permitted should match whether a command was found";
    }
}

TEST_F(PowerControllerQuery, HibernateCapabilityIsValid)
{
    auto cap = ctrl->query(WFPowerController::Action::Hibernate);
    /* Hibernate may or may not be available depending on platform */
    if (cap.available) {
        EXPECT_FALSE(cap.command.empty()) << "command must be set when available";
    }
}

TEST_F(PowerControllerQuery, SwitchUserCapabilityIsValid)
{
    auto cap = ctrl->query(WFPowerController::Action::SwitchUser);
    /* SwitchUser may or may not be available */
    if (cap.available) {
        EXPECT_FALSE(cap.command.empty()) << "command must be set when available";
        EXPECT_TRUE(cap.permitted) << "permitted should be true when available";
    }
}

TEST_F(PowerControllerQuery, AllActionsReturnValidCapability)
{
    auto actions = {
        WFPowerController::Action::Shutdown,
        WFPowerController::Action::Reboot,
        WFPowerController::Action::Suspend,
        WFPowerController::Action::Hibernate,
        WFPowerController::Action::SwitchUser,
    };
    for (auto action : actions) {
        auto cap = ctrl->query(action);
        EXPECT_FALSE(cap.permitted && !cap.available)
            << "permitted can only be true when available is true";
        if (!cap.available) {
            EXPECT_TRUE(cap.command.empty()) << "command must be empty when not available";
        }
    }
}

#if defined(__FreeBSD__)
TEST_F(PowerControllerQuery, FreeBSDPlatformSpecificRules)
{
    // On FreeBSD, Shutdown capability must be configured for wheel group permission
    auto cap_shutdown = ctrl->query(WFPowerController::Action::Shutdown);
    EXPECT_TRUE(cap_shutdown.available);
    EXPECT_EQ(cap_shutdown.command, "/sbin/shutdown -p now");
    
    // Check if the current user belongs to the wheel group (or is root)
    bool in_wheel = false;
    if (geteuid() == 0) {
        in_wheel = true;
    } else {
        gid_t wheel_gid = 0;
        struct group *gr = getgrnam("wheel");
        if (gr) {
            wheel_gid = gr->gr_gid;
            gid_t groups[64];
            int ngroups = getgroups(64, groups);
            if (ngroups >= 0) {
                for (int i = 0; i < ngroups; i++) {
                    if (groups[i] == wheel_gid) {
                        in_wheel = true;
                        break;
                    }
                }
            }
        }
    }
    
    EXPECT_EQ(cap_shutdown.permitted, in_wheel);

    // On FreeBSD, Reboot capability must be configured for wheel group permission
    auto cap_reboot = ctrl->query(WFPowerController::Action::Reboot);
    EXPECT_TRUE(cap_reboot.available);
    EXPECT_EQ(cap_reboot.command, "/sbin/shutdown -r now");
    EXPECT_EQ(cap_reboot.permitted, in_wheel);

    // On FreeBSD, Hibernate is always unavailable
    auto cap_hib = ctrl->query(WFPowerController::Action::Hibernate);
    EXPECT_FALSE(cap_hib.available);
    EXPECT_FALSE(cap_hib.permitted);
    EXPECT_TRUE(cap_hib.command.empty());
}
#endif
