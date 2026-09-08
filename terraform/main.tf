resource "cloudflare_dns_record" "vm" {
  zone_id = var.cloudflare_zone_id
  name    = local.slug
  type    = "A"
  content = "192.0.2.1"
  ttl     = 1
  comment = local.git_repository
  lifecycle {
    ignore_changes = [content]
  }
}

resource "google_service_account" "vm" {
  account_id   = "${local.slug}-vm"
  display_name = "${upper(local.slug)} VM"
}

resource "google_secret_manager_secret_iam_member" "vm_secrets" {
  for_each = toset([
    var.cloudflare_update_dns_token_secret_name,
    local.deploy_key_secret_name
  ])
  secret_id = each.value
  role      = "roles/secretmanager.secretAccessor"
  member    = "serviceAccount:${google_service_account.vm.email}"
}

# @TODO
# resource "google_compute_instance" "vm" {
#   name                      = local.slug
#   machine_type              = "e2-micro"
#   allow_stopping_for_update = true
#
#   boot_disk {
#     initialize_params {
#       image = "ubuntu-os-cloud/ubuntu-1604-xenial-v20210928"
#       type  = "pd-standard"
#       size  = 10
#     }
#   }
#
#   network_interface {
#     network = "default"
#     access_config {
#       # Ephemeral public IP
#     }
#   }
#
#   service_account {
#     email  = google_service_account.vm.email
#     scopes = ["cloud-platform"]
#   }
#
#   metadata = {
#     ssh-keys                                = "${var.ssh_username}:${trimspace(file(pathexpand(var.ssh_public_key_file)))}"
#     domain                                  = local.domain
#     cloudflare_update_dns_token_secret_name = var.cloudflare_update_dns_token_secret_name
#     git_repository                          = local.git_repository
#     git_email                               = local.git_email
#     git_name                                = local.git_name
#     cloudflare_zone_id                      = var.cloudflare_zone_id
#     cloudflare_record_id                    = cloudflare_dns_record.vm.id
#     ssh_username                            = var.ssh_username
#   }
#
#   metadata_startup_script = templatefile("${path.module}/scripts/startup.sh", {
#     SSH_USERNAME           = var.ssh_username
#     DEPLOY_KEY_SECRET_NAME = local.deploy_key_secret_name
#     GIT_REPOSITORY         = local.git_repository
#   })
# }
