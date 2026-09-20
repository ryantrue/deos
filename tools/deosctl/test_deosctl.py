import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("deosctl.py")
spec = importlib.util.spec_from_file_location("deosctl_module", MODULE_PATH)
deosctl = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(deosctl)


def manifest(resources):
    return {"apiVersion": "deos/v1alpha1", "resources": resources}


def resource(kind, name, depends=None, spec=None):
    item = {
        "kind": kind,
        "metadata": {"name": name},
        "spec": spec or {},
    }
    if depends:
        item["dependsOn"] = depends
    return item


class DeosCtlTests(unittest.TestCase):
    def test_valid_dependency_graph(self):
        doc = manifest([
            resource("Network", "wifi"),
            resource("AIProvider", "qwen", ["Network/wifi"]),
        ])
        self.assertEqual(deosctl.validate(doc), [])

    def test_missing_dependency_is_rejected(self):
        doc = manifest([resource("AIProvider", "qwen", ["Network/wifi"])])
        errors = deosctl.validate(doc)
        self.assertTrue(any("missing resource Network/wifi" in error for error in errors))

    def test_cycle_is_rejected(self):
        doc = manifest([
            resource("Service", "a", ["Service/b"]),
            resource("Service", "b", ["Service/a"]),
        ])
        errors = deosctl.validate(doc)
        self.assertTrue(any(error.startswith("dependency cycle:") for error in errors))

    def test_plan_is_deterministic(self):
        desired = manifest([
            resource("Display", "primary", spec={"brightness": 70}),
            resource("Network", "wifi"),
        ])
        actual = manifest([
            resource("Display", "primary", spec={"brightness": 50}),
            resource("Legacy", "old"),
        ])
        self.assertEqual(
            deosctl.plan(desired, actual),
            [
                ("UPDATE", "Display/primary"),
                ("CREATE", "Network/wifi"),
                ("DELETE", "Legacy/old"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
